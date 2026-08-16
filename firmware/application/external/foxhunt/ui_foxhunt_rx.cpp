/*
 * Copyright (C) 2024 HTotoo
 *
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "ui_foxhunt_rx.hpp"

#include "baseband_api.hpp"
#include "string_format.hpp"

#include <algorithm>
#include <cstring>

using namespace portapack;
namespace ui::external_app::foxhunt_rx {

namespace {
constexpr int16_t kRssiFloor = -100;
constexpr int16_t kRssiCeil = -30;
constexpr float kEmaAlpha = 0.30f;

const char* pdu_type_name(uint8_t pdu_type) {
    switch (pdu_type) {
        case 0: return "ADV_IND";
        case 1: return "ADV_DIR";
        case 2: return "ADV_NON";
        case 3: return "SCAN_REQ";
        case 4: return "SCAN_RSP";
        case 5: return "CONN_REQ";
        case 6: return "ADV_SCAN";
        default: return "OTHER";
    }
}

std::string mac_to_string(const uint8_t* mac) {
    std::string out;
    out.reserve(17);
    for (size_t i = 0; i < 6; ++i) {
        if (i) out += ":";
        out += to_string_hex(mac[i], 2);
    }
    return out;
}
}  // namespace

void FoxhuntRxView::focus() {
    button_next.focus();
}

FoxhuntRxView::FoxhuntRxView(NavigationView& nav)
    : nav_{nav} {
    baseband::run_image(portapack::spi_flash::image_tag_btle_rx);

    add_children({&rssi,
                  &field_rf_amp,
                  &field_lna,
                  &field_vga,
                  &field_frequency,
                  &channel,
                  &button_next,
                  &button_lock,
                  &button_page,
                  &options_info_mode,
                  &options_list_mode,
                  &options_sort_mode,
                  &text_cfg_title,
                  &text_cfg_hint,
                  &text_cfg_footer,
                  &text_status,
                  &text_found,
                  &text_target,
                  &text_current,
                  &text_ema,
                  &text_pps,
                  &text_name,
                  &text_info,
                  &text_row_0,
                  &text_row_1,
                  &text_row_2,
                  &text_row_3,
                  &rssi_graph,
                  });

    field_frequency.set_step(0);
    field_frequency.set_value(channel_to_freq(channel_number_));
    rssi_graph.set_nb_columns(120);

    options_info_mode.on_change = [this](size_t, int32_t v) {
        info_mode_ = static_cast<uint8_t>(v);
        update_ui_text();
    };
    options_list_mode.on_change = [this](size_t, int32_t v) {
        list_mode_ = static_cast<uint8_t>(v);
        update_ui_text();
    };
    options_sort_mode.on_change = [this](size_t, int32_t v) {
        sort_mode_ = static_cast<uint8_t>(v);
        rebuild_sorted_indices();
        update_ui_text();
    };

    button_next.on_select = [this](Button&) {
        if (sorted_count_ == 0) return;
        selected_rank_ = (selected_rank_ + 1) % sorted_count_;
        if (lock_enabled_) locked_device_id_ = devices_[sorted_indices_[selected_rank_]].device_id;
        tracked_rate_device_id_ = 0;
        tracked_last_packet_count_ = 0;
        tracked_pps_ = 0;
        update_ui_text();
    };

    button_lock.on_select = [this](Button&) {
        if (!lock_enabled_) {
            if (sorted_count_ == 0) return;
            lock_enabled_ = true;
            locked_device_id_ = devices_[sorted_indices_[selected_rank_]].device_id;
            button_lock.set_text("Unlock");
        } else {
            lock_enabled_ = false;
            locked_device_id_ = 0;
            button_lock.set_text("Lock");
        }
        tracked_rate_device_id_ = 0;
        tracked_last_packet_count_ = 0;
        tracked_pps_ = 0;
        update_ui_text();
    };

    button_page.on_select = [this](Button&) {
        set_config_page(!config_page_);
    };

    options_info_mode.set_by_value(info_mode_);
    options_list_mode.set_by_value(list_mode_);
    options_sort_mode.set_by_value(sort_mode_);

    baseband::set_btlerx(channel_number_);
    receiver_model.enable();

    set_config_page(false);
    update_ui_text();
}

FoxhuntRxView::~FoxhuntRxView() {
    receiver_model.disable();
    baseband::shutdown();
}

uint64_t FoxhuntRxView::channel_to_freq(uint8_t channel_number) const {
    if (channel_number == 37) return 2402000000ull;
    if (channel_number == 38) return 2426000000ull;
    return 2480000000ull;
}

bool FoxhuntRxView::parse_ef28_payload(const BlePacketData* packet, EF28Packet& out) const {
    uint8_t offset = 0;
    while (offset < packet->dataLen) {
        const uint8_t ad_len = packet->data[offset++];
        if (ad_len == 0) break;
        if ((offset + ad_len) > packet->dataLen) break;

        const uint8_t ad_type = packet->data[offset++];
        const uint8_t* ad_data = &packet->data[offset];
        const uint8_t ad_data_len = ad_len - 1;

        if (ad_type == kManufacturerType && ad_data_len >= 10) {
            const uint16_t company_id = static_cast<uint16_t>(ad_data[0]) | (static_cast<uint16_t>(ad_data[1]) << 8);
            const uint8_t version = ad_data[2];
            if (company_id == kCompanyId && version == kVersion) {
                out.type = ad_data[3];
                out.device_id = static_cast<uint32_t>(ad_data[4]) |
                                (static_cast<uint32_t>(ad_data[5]) << 8) |
                                (static_cast<uint32_t>(ad_data[6]) << 16) |
                                (static_cast<uint32_t>(ad_data[7]) << 24);
                out.flags = ad_data[8];
                out.tx_power = static_cast<int8_t>(ad_data[9]);
                return true;
            }
        }
        offset += ad_data_len;
    }
    return false;
}

int FoxhuntRxView::find_device(uint32_t device_id) const {
    for (uint16_t i = 0; i < kMaxDevices; ++i) {
        if (devices_[i].used && devices_[i].device_id == device_id) return i;
    }
    return -1;
}

int FoxhuntRxView::find_device_by_mac(const uint8_t* mac) const {
    for (uint16_t i = 0; i < kMaxDevices; ++i) {
        if (devices_[i].used && std::memcmp(devices_[i].mac, mac, 6) == 0) return i;
    }
    return -1;
}

int FoxhuntRxView::alloc_device(uint32_t device_id) {
    for (uint16_t i = 0; i < kMaxDevices; ++i) {
        if (!devices_[i].used) {
            devices_[i] = EF28Device{};
            devices_[i].used = true;
            devices_[i].device_id = device_id;
            devices_[i].seen_order = next_seen_order_++;
            return i;
        }
    }

    uint16_t oldest_idx = 0;
    systime_t oldest_seen = (systime_t)-1;
    for (uint16_t i = 0; i < kMaxDevices; ++i) {
        if (devices_[i].last_seen_tick < oldest_seen) {
            oldest_seen = devices_[i].last_seen_tick;
            oldest_idx = i;
        }
    }
    devices_[oldest_idx] = EF28Device{};
    devices_[oldest_idx].used = true;
    devices_[oldest_idx].device_id = device_id;
    devices_[oldest_idx].seen_order = next_seen_order_++;
    return oldest_idx;
}

void FoxhuntRxView::on_data(BlePacketData* packet) {
    const int16_t corrected_rssi = packet->max_dB - (receiver_model.lna() + receiver_model.vga() + (receiver_model.rf_amp() ? 14 : 0));
    const systime_t now = chTimeNow();

    const int idx_by_mac = find_device_by_mac(packet->macAddress);
    if (idx_by_mac >= 0) {
        auto& m = devices_[idx_by_mac];
        m.pdu_type = packet->type;
        std::memcpy(m.mac, packet->macAddress, sizeof(m.mac));
        extract_local_name(packet, m);
        m.rssi = corrected_rssi;
        m.ema_rssi = (m.ema_rssi == -127) ? corrected_rssi : static_cast<int16_t>(kEmaAlpha * corrected_rssi + (1.0f - kEmaAlpha) * m.ema_rssi);
        m.last_seen_tick = now;
    }

    EF28Packet parsed{};
    if (!parse_ef28_payload(packet, parsed)) {
        if (idx_by_mac >= 0) rebuild_sorted_indices();
        return;
    }

    int idx = find_device(parsed.device_id);
    if (idx < 0 && idx_by_mac >= 0) idx = idx_by_mac;
    if (idx < 0) idx = alloc_device(parsed.device_id);

    auto& dev = devices_[idx];
    dev.device_id = parsed.device_id;
    dev.type = parsed.type;
    dev.pdu_type = packet->type;
    dev.flags = parsed.flags;
    std::memcpy(dev.mac, packet->macAddress, sizeof(dev.mac));
    dev.tx_power = parsed.tx_power;
    extract_local_name(packet, dev);
    dev.rssi = corrected_rssi;
    dev.ema_rssi = (dev.ema_rssi == -127) ? corrected_rssi : static_cast<int16_t>(kEmaAlpha * corrected_rssi + (1.0f - kEmaAlpha) * dev.ema_rssi);
    dev.last_seen_tick = now;
    dev.packet_count++;

    rebuild_sorted_indices();
    update_ui_text();
}

bool FoxhuntRxView::device_is_fresh(const EF28Device& dev, systime_t now) const {
    return dev.used && ((now - dev.last_seen_tick) <= (7 * CH_FREQUENCY));
}

void FoxhuntRxView::rebuild_sorted_indices() {
    sorted_count_ = 0;
    const systime_t now = chTimeNow();
    for (uint8_t i = 0; i < kMaxDevices; ++i) {
        if (device_is_fresh(devices_[i], now)) sorted_indices_[sorted_count_++] = i;
    }

    for (uint8_t i = 0; i < sorted_count_; ++i) {
        for (uint8_t j = i + 1; j < sorted_count_; ++j) {
            const auto& a = devices_[sorted_indices_[i]];
            const auto& b = devices_[sorted_indices_[j]];
            const bool swap = (sort_mode_ == 1) ? (b.rssi > a.rssi) : (b.seen_order < a.seen_order);
            if (swap) {
                const uint8_t tmp = sorted_indices_[i];
                sorted_indices_[i] = sorted_indices_[j];
                sorted_indices_[j] = tmp;
            }
        }
    }

    if (sorted_count_ == 0) selected_rank_ = 0;
    else if (selected_rank_ >= sorted_count_) selected_rank_ = sorted_count_ - 1;
}

int FoxhuntRxView::tracked_device_index() const {
    if (lock_enabled_) return find_device(locked_device_id_);
    if (sorted_count_ == 0) return -1;
    return sorted_indices_[selected_rank_];
}

std::string FoxhuntRxView::device_display_name(const EF28Device& dev) const {
    if (list_mode_ == 1 && dev.has_name) {
        std::string n = dev.name.data();
        if (n.size() > 11) n.resize(11);
        return n;
    }
    return to_string_hex(dev.device_id, 8);
}

void FoxhuntRxView::push_graph_sample(int16_t rssi_now, int16_t rssi_ema) {
    if (rssi_now < kRssiFloor) rssi_now = kRssiFloor;
    if (rssi_now > kRssiCeil) rssi_now = kRssiCeil;
    if (rssi_ema < kRssiFloor) rssi_ema = kRssiFloor;
    if (rssi_ema > kRssiCeil) rssi_ema = kRssiCeil;
    rssi_graph.add_values(rssi_now, rssi_ema, rssi_now, rssi_now);
}

void FoxhuntRxView::set_config_page(bool enabled) {
    if (enabled) {
        text_status.set("                                ");
        text_found.set("                                ");
        text_target.set("                                ");
        text_current.set("                                ");
        text_ema.set("                                ");
        text_pps.set("                                ");
        text_name.set("                                ");
        text_info.set("                                ");
        text_row_0.set("                                ");
        text_row_1.set("                                ");
        text_row_2.set("                                ");
        text_row_3.set("                                ");
    } else {
        text_cfg_title.set("                                ");
        text_cfg_hint.set("                                ");
        text_cfg_footer.set("                                ");
    }

    config_page_ = enabled;

    button_next.hidden(enabled);
    button_lock.hidden(enabled);
    text_status.hidden(enabled);
    text_found.hidden(enabled);
    text_target.hidden(enabled);
    text_current.hidden(enabled);
    text_ema.hidden(enabled);
    text_pps.hidden(enabled);
    text_name.hidden(enabled);
    text_info.hidden(enabled);
    text_row_0.hidden(enabled);
    text_row_1.hidden(enabled);
    text_row_2.hidden(enabled);
    text_row_3.hidden(enabled);
    rssi_graph.hidden(enabled);

    options_info_mode.hidden(!enabled);
    options_list_mode.hidden(!enabled);
    options_sort_mode.hidden(!enabled);
    text_cfg_title.hidden(!enabled);
    text_cfg_hint.hidden(!enabled);
    text_cfg_footer.hidden(!enabled);

    if (enabled) {
        text_cfg_title.set("EF28 RX V17 RESTORE");
        text_cfg_hint.set("Graph = v12 known good");
        text_cfg_footer.set("Clear test / RX only");
    }

    button_page.set_text(enabled ? "Back" : "Cfg");
    if (!enabled) update_ui_text();
}

void FoxhuntRxView::update_ui_text() {
    const int tracked_idx = tracked_device_index();

    std::string status = lock_enabled_ ? "RX LOCK" : "RX TRACK";
    text_status.set(status);
    text_found.set("F:" + to_string_dec_uint(sorted_count_));

    if (tracked_idx >= 0) {
        const auto& t = devices_[tracked_idx];
        text_target.set("#" + to_string_dec_uint(t.seen_order) + " " + to_string_hex(t.device_id, 8));
        text_current.set("RSSI:" + to_string_dec_int(t.rssi));
        text_ema.set("EMA:" + to_string_dec_int(t.ema_rssi));
        text_pps.set("PPS:" + to_string_dec_uint(tracked_pps_));
        text_name.set(t.has_name ? (std::string("Name:") + t.name.data()) : "Name:--");

        std::string info;
        if (info_mode_ == 1) {
            info = "MAC:" + mac_to_string(t.mac);
        } else if (info_mode_ == 2) {
            info = std::string("PDU:") + pdu_type_name(t.pdu_type) + " F:" + to_string_hex(t.flags, 2) + " P:" + to_string_dec_int(t.tx_power);
        } else {
            info = (t.type == 'D') ? "Type:Badge" : ((t.type == 'B') ? "Type:Beacon" : "Type:Unknown");
            info += " Sort:";
            info += (sort_mode_ == 1) ? "RSSI" : "Seen";
        }
        text_info.set(info);
    } else {
        text_target.set("T:--");
        text_current.set("RSSI:--");
        text_ema.set("EMA:--");
        text_pps.set("PPS:0");
        text_name.set("Name:--");
        text_info.set("No EF28 packets");
    }

    std::array<Text*, kVisibleRows> rows{{&text_row_0, &text_row_1, &text_row_2, &text_row_3}};
    for (uint8_t i = 0; i < kVisibleRows; ++i) {
        if (i >= sorted_count_) {
            rows[i]->set("");
            continue;
        }
        const auto& d = devices_[sorted_indices_[i]];
        const bool selected = (i == selected_rank_);
        const uint32_t age_s = static_cast<uint32_t>((chTimeNow() - d.last_seen_tick) / CH_FREQUENCY);

        std::string line = selected ? ">" : " ";
        line += "#" + to_string_dec_uint(d.seen_order) + " ";
        line += device_display_name(d);
        line += " ";
        line += (d.type == 'D') ? "D " : ((d.type == 'B') ? "B " : "? ");
        line += to_string_dec_int(d.rssi);
        line += " ";
        line += to_string_dec_uint(age_s);
        line += "s";
        rows[i]->set(line);
    }
}

void FoxhuntRxView::extract_local_name(const BlePacketData* packet, EF28Device& dev) const {
    uint8_t offset = 0;
    while (offset < packet->dataLen) {
        const uint8_t ad_len = packet->data[offset++];
        if (ad_len == 0) break;
        if ((offset + ad_len) > packet->dataLen) break;

        const uint8_t ad_type = packet->data[offset++];
        const uint8_t* ad_data = &packet->data[offset];
        const uint8_t ad_data_len = ad_len - 1;

        if (ad_type == 0x08 || ad_type == 0x09) {
            const size_t max_len = dev.name.size() - 1;
            const size_t copy_len = std::min(max_len, static_cast<size_t>(ad_data_len));
            for (size_t i = 0; i < copy_len; ++i) {
                const char c = static_cast<char>(ad_data[i]);
                dev.name[i] = (c >= 32 && c <= 126) ? c : '?';
            }
            dev.name[copy_len] = '\0';
            dev.has_name = copy_len > 0;
            return;
        }
        offset += ad_data_len;
    }
}

void FoxhuntRxView::update_channel_hop() {
    baseband::set_btlerx(channel_number_);
    field_frequency.set_value(channel_to_freq(channel_number_));
    if (channel_number_ == 37) channel_number_ = 38;
    else if (channel_number_ == 38) channel_number_ = 39;
    else channel_number_ = 37;
}

void FoxhuntRxView::on_timer() {
    if (++hop_timer_count_ >= 6) {
        hop_timer_count_ = 0;
        update_channel_hop();
    }

    if (++ui_timer_count_ >= 6) {
        ui_timer_count_ = 0;
        rebuild_sorted_indices();

        const int tracked_idx = tracked_device_index();
        if (tracked_idx >= 0) {
            auto& t = devices_[tracked_idx];
            if (tracked_rate_device_id_ == t.device_id) tracked_pps_ = t.packet_count - tracked_last_packet_count_;
            else tracked_pps_ = 0;

            const bool got_new_packet = (t.packet_count != tracked_last_packet_count_) || (tracked_rate_device_id_ != t.device_id);
            tracked_rate_device_id_ = t.device_id;
            tracked_last_packet_count_ = t.packet_count;

            if (got_new_packet) push_graph_sample(t.rssi, t.ema_rssi);
        } else {
            tracked_pps_ = 0;
            tracked_rate_device_id_ = 0;
            tracked_last_packet_count_ = 0;
        }
        update_ui_text();
    }
}

}  // namespace ui::external_app::foxhunt_rx
