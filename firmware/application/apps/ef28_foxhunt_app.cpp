/*
 * Copyright (C) 2026
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

#include "ef28_foxhunt_app.hpp"

#include "baseband_api.hpp"
#include "string_format.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace portapack;

namespace ui {

namespace {

constexpr int16_t kRssiFloor = -100;
constexpr int16_t kRssiCeil = -30;
constexpr float kEmaAlpha = 0.30f;
constexpr std::array<uint8_t, 3> kAdvChannels{{37, 38, 39}};

const char* pdu_type_name(uint8_t pdu_type) {
    switch (pdu_type) {
        case 0:
            return "ADV_IND";
        case 1:
            return "ADV_DIR";
        case 2:
            return "ADV_NON";
        case 3:
            return "SCAN_REQ";
        case 4:
            return "SCAN_RSP";
        case 5:
            return "CONN_REQ";
        case 6:
            return "ADV_SCAN";
        default:
            return "OTHER";
    }
}

std::string mac_to_string(const uint8_t* mac) {
    std::string out;
    out.reserve(17);

    for (size_t i = 0; i < 6; ++i) {
        if (i != 0) {
            out += ":";
        }
        out += to_string_hex(mac[i], 2);
    }
    return out;
}

}  // namespace

void EF28FoxHuntView::focus() {
    button_next.focus();
}

EF28FoxHuntView::EF28FoxHuntView(NavigationView& nav)
    : nav_{nav} {
    baseband::run_image(portapack::spi_flash::image_tag_btle_rx);

    add_children({&field_frequency,
                  &field_rf_amp,
                  &field_lna,
                  &field_vga,
                  &rssi,
                  &channel,
                  &button_next,
                  &button_lock,
                  &button_reply,
                  &button_page,
                  &options_response_mode,
                  &options_identity,
                  &options_style,
                  &options_tx_power,
                  &options_display_mode,
                  &options_list_mode,
                  &options_name_profile,
                  &options_flags_mode,
                  &options_reply_pdu,
                  &options_reply_filter,
                  &button_edit_name,
                  &text_cfg_title,
                  &text_cfg_hint,
                  &text_cfg_disp,
                  &text_cfg_reply,
                  &text_cfg_name,
                  &text_cfg_footer,
                  &text_status,
                  &text_found,
                  &text_target,
                  &text_current,
                  &text_ema,
                  &text_pps,
                  &text_name,
                  &text_info,
                  &text_tx,
                  &text_row_0,
                  &text_row_1,
                  &text_row_2,
                  &text_row_3,
                  &text_row_4,
                  &rssi_graph});

    field_frequency.set_step(0);
    field_frequency.set_value(channel_to_freq(channel_number_));

    rssi_graph.set_nb_columns(120);

    options_response_mode.on_change = [this](size_t, int32_t v) {
        response_mode_ = static_cast<uint8_t>(v);
        if (response_mode_ == 0 && tx_active_) {
            stop_tx_response();
        }
        update_ui_text();
    };

    options_identity.on_change = [this](size_t, int32_t v) {
        response_identity_ = static_cast<uint8_t>(v);
        update_ui_text();
    };

    options_style.on_change = [this](size_t, int32_t v) {
        response_style_ = static_cast<uint8_t>(v);
        update_ui_text();
    };

    options_tx_power.on_change = [this](size_t, int32_t v) {
        response_tx_power_ = static_cast<int8_t>(v);
        update_ui_text();
    };

    options_display_mode.on_change = [this](size_t, int32_t v) {
        info_mode_ = static_cast<uint8_t>(v);
        update_ui_text();
    };

    options_list_mode.on_change = [this](size_t, int32_t v) {
        list_mode_ = static_cast<uint8_t>(v);
        update_ui_text();
    };

    options_name_profile.on_change = [this](size_t, int32_t v) {
        name_profile_ = static_cast<uint8_t>(v);
        update_ui_text();
    };

    options_flags_mode.on_change = [this](size_t, int32_t v) {
        response_flags_mode_ = static_cast<uint8_t>(v);
        update_ui_text();
    };

    options_reply_pdu.on_change = [this](size_t, int32_t v) {
        response_pdu_ = static_cast<uint8_t>(v);
        update_ui_text();
    };

    options_reply_filter.on_change = [this](size_t, int32_t v) {
        response_filter_ = static_cast<uint8_t>(v);
        update_ui_text();
    };

    button_next.on_select = [this](Button&) {
        if (sorted_count_ == 0) {
            return;
        }
        selected_rank_ = (selected_rank_ + 1) % sorted_count_;
        if (lock_enabled_) {
            locked_device_id_ = devices_[sorted_indices_[selected_rank_]].device_id;
        }
        update_ui_text();
    };

    button_lock.on_select = [this](Button&) {
        if (!lock_enabled_) {
            if (sorted_count_ == 0) {
                return;
            }
            lock_enabled_ = true;
            locked_device_id_ = devices_[sorted_indices_[selected_rank_]].device_id;
            tracked_rate_device_id_ = 0;
            tracked_last_packet_count_ = 0;
            tracked_pps_ = 0;
            button_lock.set_text("Unlock");
        } else {
            lock_enabled_ = false;
            locked_device_id_ = 0;
            tracked_rate_device_id_ = 0;
            tracked_last_packet_count_ = 0;
            tracked_pps_ = 0;
            button_lock.set_text("Lock");
        }
        update_ui_text();
    };

    button_reply.on_select = [this](Button&) {
        if (tx_active_) {
            return;
        }

        const int tracked_idx = tracked_device_index();
        if (tracked_idx < 0) {
            return;
        }

        queue_response_for(devices_[tracked_idx]);
        tx_pending_ = true;
        tx_request_count_++;
        update_ui_text();
    };

    button_page.on_select = [this](Button&) {
        set_config_page(!config_page_);
    };

    button_edit_name.on_select = [this](Button&) {
        custom_name_edit_buffer_ = custom_tx_name_.data();
        text_prompt(
            nav_,
            custom_name_edit_buffer_,
            custom_tx_name_.size() - 1,
            ENTER_KEYBOARD_MODE_ALPHA,
            [this](std::string& buffer) {
                if (buffer.empty()) {
                    buffer = "EF28-Test";
                }
                const size_t copy_len = std::min(buffer.size(), custom_tx_name_.size() - 1);
                std::memset(custom_tx_name_.data(), 0, custom_tx_name_.size());
                std::memcpy(custom_tx_name_.data(), buffer.data(), copy_len);
                name_profile_ = 4;
                options_name_profile.set_by_value(4);
                update_ui_text();
            });
    };

    options_response_mode.set_by_value(response_mode_);
    options_identity.set_by_value(response_identity_);
    options_style.set_by_value(response_style_);
    options_tx_power.set_by_value(response_tx_power_);
    options_display_mode.set_by_value(info_mode_);
    options_list_mode.set_by_value(list_mode_);
    options_name_profile.set_by_value(name_profile_);
    options_flags_mode.set_by_value(response_flags_mode_);
    options_reply_pdu.set_by_value(response_pdu_);
    options_reply_filter.set_by_value(response_filter_);

    baseband::set_btlerx(channel_number_);
    receiver_model.enable();

    set_config_page(false);
    update_ui_text();
}

EF28FoxHuntView::~EF28FoxHuntView() {
    receiver_model.disable();
    transmitter_model.disable();
    baseband::shutdown();
}

uint64_t EF28FoxHuntView::channel_to_freq(uint8_t channel) const {
    if (channel == 37) return 2402000000ull;
    if (channel == 38) return 2426000000ull;
    return 2480000000ull;
}

bool EF28FoxHuntView::parse_ef28_payload(const BlePacketData* packet, EF28Packet& out) const {
    uint8_t offset = 0;

    while (offset < packet->dataLen) {
        const uint8_t ad_len = packet->data[offset++];

        if (ad_len == 0) {
            break;
        }

        if ((offset + ad_len) > packet->dataLen) {
            break;
        }

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

int EF28FoxHuntView::find_device(uint32_t device_id) const {
    for (uint16_t i = 0; i < kMaxDevices; ++i) {
        if (devices_[i].used && devices_[i].device_id == device_id) {
            return i;
        }
    }
    return -1;
}

int EF28FoxHuntView::find_device_by_mac(const uint8_t* mac) const {
    for (uint16_t i = 0; i < kMaxDevices; ++i) {
        if (devices_[i].used && std::memcmp(devices_[i].mac, mac, 6) == 0) {
            return i;
        }
    }
    return -1;
}

int EF28FoxHuntView::alloc_device(uint32_t device_id) {
    for (uint16_t i = 0; i < kMaxDevices; ++i) {
        if (!devices_[i].used) {
            devices_[i] = EF28Device{};
            devices_[i].used = true;
            devices_[i].device_id = device_id;
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
    return oldest_idx;
}

void EF28FoxHuntView::on_data(BlePacketData* packet) {
    const int16_t corrected_rssi = packet->max_dB - (receiver_model.lna() + receiver_model.vga() + (receiver_model.rf_amp() ? 14 : 0));
    const systime_t now = chTimeNow();

    // Names are often carried in separate ADV/SCAN packets without our manufacturer block.
    const int idx_by_mac = find_device_by_mac(packet->macAddress);
    if (idx_by_mac >= 0) {
        auto& dev_by_mac = devices_[idx_by_mac];
        dev_by_mac.pdu_type = packet->type;
        std::memcpy(dev_by_mac.mac, packet->macAddress, sizeof(dev_by_mac.mac));
        extract_local_name(packet, dev_by_mac);
        dev_by_mac.rssi = corrected_rssi;

        if (dev_by_mac.ema_rssi == -127) {
            dev_by_mac.ema_rssi = corrected_rssi;
        } else {
            dev_by_mac.ema_rssi = static_cast<int16_t>(kEmaAlpha * corrected_rssi + (1.0f - kEmaAlpha) * dev_by_mac.ema_rssi);
        }

        dev_by_mac.last_seen_tick = now;
    }

    EF28Packet parsed{};
    if (!parse_ef28_payload(packet, parsed)) {
        if (idx_by_mac >= 0) {
            rebuild_sorted_indices();
        }
        return;
    }

    int idx = find_device(parsed.device_id);
    if (idx < 0 && idx_by_mac >= 0) {
        idx = idx_by_mac;
    }
    if (idx < 0) {
        idx = alloc_device(parsed.device_id);
    }

    auto& dev = devices_[idx];

    dev.device_id = parsed.device_id;
    dev.type = parsed.type;
    dev.pdu_type = packet->type;
    dev.flags = parsed.flags;
    std::memcpy(dev.mac, packet->macAddress, sizeof(dev.mac));
    dev.tx_power = parsed.tx_power;
    extract_local_name(packet, dev);
    dev.rssi = corrected_rssi;

    if (dev.ema_rssi == -127) {
        dev.ema_rssi = corrected_rssi;
    } else {
        dev.ema_rssi = static_cast<int16_t>(kEmaAlpha * corrected_rssi + (1.0f - kEmaAlpha) * dev.ema_rssi);
    }

    dev.last_seen_tick = now;
    dev.packet_count++;

    rebuild_sorted_indices();

    if (!tx_active_ && response_mode_ == 2 && tx_cooldown_ == 0 && should_auto_reply_to(dev)) {
        queue_response_for(dev);
        tx_pending_ = true;
        tx_request_count_++;
    }
}

void EF28FoxHuntView::rebuild_sorted_indices() {
    sorted_count_ = 0;

    const systime_t now = chTimeNow();
    for (uint8_t i = 0; i < kMaxDevices; ++i) {
        if (device_is_fresh(devices_[i], now)) {
            sorted_indices_[sorted_count_++] = i;
        }
    }

    for (uint8_t i = 0; i < sorted_count_; ++i) {
        for (uint8_t j = i + 1; j < sorted_count_; ++j) {
            if (devices_[sorted_indices_[j]].rssi > devices_[sorted_indices_[i]].rssi) {
                const uint8_t tmp = sorted_indices_[i];
                sorted_indices_[i] = sorted_indices_[j];
                sorted_indices_[j] = tmp;
            }
        }
    }

    if (sorted_count_ == 0) {
        selected_rank_ = 0;
    } else if (selected_rank_ >= sorted_count_) {
        selected_rank_ = sorted_count_ - 1;
    }
}

int EF28FoxHuntView::strongest_device_index() const {
    if (sorted_count_ == 0) {
        return -1;
    }
    return sorted_indices_[0];
}

int EF28FoxHuntView::tracked_device_index() const {
    if (lock_enabled_) {
        return find_device(locked_device_id_);
    }
    return strongest_device_index();
}

bool EF28FoxHuntView::device_is_fresh(const EF28Device& dev, systime_t now) const {
    return dev.used && ((now - dev.last_seen_tick) <= (10 * CH_FREQUENCY));
}

std::string EF28FoxHuntView::device_display_name(const EF28Device& dev) const {
    if (list_mode_ == 1 && dev.has_name) {
        std::string n = dev.name.data();
        if (n.size() > 10) {
            n.resize(10);
        }
        return n;
    }
    return to_string_hex(dev.device_id, 8);
}

void EF28FoxHuntView::push_graph_samples() {
    const systime_t now = chTimeNow();
    std::array<int16_t, 4> values{{kRssiFloor, kRssiFloor, kRssiFloor, kRssiFloor}};

    for (uint8_t out = 0, rank = 0; rank < sorted_count_ && out < values.size(); ++rank) {
        const auto& d = devices_[sorted_indices_[rank]];
        const systime_t age = now - d.last_seen_tick;

        // Keep active trends visible. When a badge goes stale, stop extending it by
        // sending the floor value only after the freshness window. This avoids the
        // old fake horizontal trace while not killing the live graph too quickly.
        int16_t v = (age <= (10 * CH_FREQUENCY)) ? d.ema_rssi : kRssiFloor;
        if (v < kRssiFloor) v = kRssiFloor;
        if (v > kRssiCeil) v = kRssiCeil;
        values[out++] = v;
    }

    rssi_graph.add_values(values[0], values[1], values[2], values[3]);
}

void EF28FoxHuntView::set_config_page(bool enabled) {
    config_page_ = enabled;

    button_next.hidden(enabled);
    button_lock.hidden(enabled);
    button_reply.hidden(enabled);
    text_status.hidden(enabled);
    text_found.hidden(enabled);
    text_target.hidden(enabled);
    text_current.hidden(enabled);
    text_ema.hidden(enabled);
    text_pps.hidden(enabled);
    text_name.hidden(enabled);
    text_info.hidden(enabled);
    text_tx.hidden(enabled);
    text_row_0.hidden(enabled);
    text_row_1.hidden(enabled);
    text_row_2.hidden(enabled);
    text_row_3.hidden(enabled);
    text_row_4.hidden(enabled);
    rssi_graph.hidden(enabled);

    options_response_mode.hidden(!enabled);
    options_identity.hidden(!enabled);
    options_style.hidden(!enabled);
    options_tx_power.hidden(!enabled);
    options_display_mode.hidden(!enabled);
    options_list_mode.hidden(!enabled);
    options_name_profile.hidden(!enabled);
    options_flags_mode.hidden(!enabled);
    options_reply_pdu.hidden(!enabled);
    options_reply_filter.hidden(!enabled);
    button_edit_name.hidden(!enabled);
    text_cfg_title.hidden(!enabled);
    text_cfg_hint.hidden(!enabled);
    text_cfg_disp.hidden(!enabled);
    text_cfg_reply.hidden(!enabled);
    text_cfg_name.hidden(!enabled);
    text_cfg_footer.hidden(!enabled);

    button_page.set_text(enabled ? "Back" : "Cfg");
}

void EF28FoxHuntView::update_ui_text() {
    const int tracked_idx = tracked_device_index();

    std::string status = lock_enabled_ ? "LOCK" : "TRACK";
    status += tx_active_ ? " TX" : " RX";
    text_status.set(status);
    text_found.set("F:" + to_string_dec_uint(sorted_count_));

    if (tracked_idx >= 0) {
        const auto& t = devices_[tracked_idx];
        text_target.set("T:" + to_string_hex(t.device_id, 8));
        text_current.set("RSSI: " + to_string_dec_int(t.rssi));
        text_ema.set("EMA: " + to_string_dec_int(t.ema_rssi));
        text_pps.set("PPS: " + to_string_dec_uint(tracked_pps_));

        if (t.has_name) {
            text_name.set(std::string("Name: ") + t.name.data());
        } else {
            text_name.set("Name: --");
        }

        std::string info;
        switch (info_mode_) {
            case 0:
                info = "ID:" + to_string_hex(t.device_id, 8);
                info += " ";
                info += (t.type == 'D') ? "Badge" : ((t.type == 'B') ? "Beacon" : "Unknown");
                break;
            case 1:
                info = "MAC:" + mac_to_string(t.mac);
                break;
            case 2:
                info = std::string("PDU:") + pdu_type_name(t.pdu_type);
                info += " F:" + to_string_hex(t.flags, 2);
                info += " P:" + to_string_dec_int(t.tx_power);
                break;
            default:
                info = "TX cfg ";
                info += (response_pdu_ == kPktDiscovery) ? "DISC" : ((response_pdu_ == kPktAdvInd) ? "ADV" : "NON");
                info += " ";
                info += (response_identity_ == 0) ? "Badge" : "Beacon";
                break;
        }
        text_info.set(info);

        std::string tx_line = "TX:";
        tx_line += tx_active_ ? "ON " : (tx_pending_ ? "QUEUED " : "idle ");
        tx_line += "req:" + to_string_dec_uint(tx_request_count_);
        tx_line += " ok:" + to_string_dec_uint(tx_done_count_);
        text_tx.set(tx_line);
    } else {
        text_target.set("T:--");
        text_current.set("RSSI: --");
        text_ema.set("EMA: --");
        text_pps.set("PPS: 0");
        text_name.set("Name: --");
        text_info.set("MAC: --");
        text_tx.set("TX: idle");
    }

    const char* cfg_name = "EF28-Test";
    if (name_profile_ == 1) cfg_name = "EF28-HackRF";
    else if (name_profile_ == 2) cfg_name = "EF28-Fox";
    else if (name_profile_ == 3) cfg_name = "EF28-Jenna";
    else if (name_profile_ == 4) cfg_name = custom_tx_name_.data();
    text_cfg_name.set(std::string("TX name: ") + cfg_name);

    std::array<Text*, kVisibleRows> rows{{&text_row_0, &text_row_1, &text_row_2, &text_row_3, &text_row_4}};

    for (uint8_t i = 0; i < kVisibleRows; ++i) {
        if (i >= sorted_count_) {
            rows[i]->set("");
            continue;
        }

        const uint8_t idx = sorted_indices_[i];
        const auto& d = devices_[idx];
        const uint32_t age_s = static_cast<uint32_t>((chTimeNow() - d.last_seen_tick) / CH_FREQUENCY);
        const bool selected = (i == selected_rank_);

        std::string line = selected ? ">" : " ";
        line += "G";
        line += to_string_dec_uint(i + 1);
        line += " ";
        line += device_display_name(d);
        line += " ";
        line += (d.type == 'D') ? "D" : ((d.type == 'B') ? "B" : "?");
        line += " ";
        line += to_string_dec_int(d.rssi);
        line += "dB ";
        line += to_string_dec_uint(age_s);
        line += "s";

        rows[i]->set(line);
    }

    if (response_mode_ == 0) {
        button_reply.set_text("Send Once");
    } else if (tx_active_) {
        button_reply.set_text("Sending");
    } else if (response_mode_ == 2) {
        button_reply.set_text("Auto Armed");
    } else {
        button_reply.set_text("Reply Now");
    }
}

void EF28FoxHuntView::extract_local_name(const BlePacketData* packet, EF28Device& dev) const {
    uint8_t offset = 0;

    while (offset < packet->dataLen) {
        const uint8_t ad_len = packet->data[offset++];

        if (ad_len == 0) {
            break;
        }

        if ((offset + ad_len) > packet->dataLen) {
            break;
        }

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

void EF28FoxHuntView::queue_response_for(const EF28Device& peer) {
    const uint8_t my_type = (response_identity_ == 0) ? static_cast<uint8_t>('D') : static_cast<uint8_t>('B');
    const uint32_t out_id = (response_style_ == 0) ? my_device_id_ : peer.device_id;

    const uint8_t flags = response_flags_for(peer);
    const uint8_t txp = static_cast<uint8_t>(response_tx_power_);

    const char* name = "EF28-Test";
    if (name_profile_ == 1) {
        name = "EF28-HackRF";
    } else if (name_profile_ == 2) {
        name = "EF28-Fox";
    } else if (name_profile_ == 3) {
        name = "EF28-Jenna";
    } else if (name_profile_ == 4) {
        name = custom_tx_name_.data();
    }

    char name_ad[31]{};
    const size_t name_len = std::min<size_t>(std::strlen(name), 17);
    std::snprintf(name_ad,
                  sizeof(name_ad),
                  "%02X09",
                  static_cast<unsigned>(name_len + 1));
    for (size_t i = 0; i < name_len; ++i) {
        char byte_hex[3]{};
        std::snprintf(byte_hex, sizeof(byte_hex), "%02X", static_cast<unsigned>(static_cast<uint8_t>(name[i])));
        std::strncat(name_ad, byte_hex, sizeof(name_ad) - std::strlen(name_ad) - 1);
    }

    // Exact known-good order from DP EF28-Test FULL badge manufacturer first.txt:
    // 0B FF EF 28 02 44 01 00 28 EF 09 07  0A 09 45 46 32 38 2D 54 65 73 74
    // i.e. manufacturer data first, then Complete Local Name. The BLE TX app uses
    // packet type DISCOVERY for this file, so the default pdu enum is kPktDiscovery.
    std::snprintf(tx_adv_,
                  sizeof(tx_adv_),
                  "0BFFEF2802%02X%02X%02X%02X%02X%02X%s",
                  my_type,
                  static_cast<uint8_t>(out_id & 0xFF),
                  static_cast<uint8_t>((out_id >> 8) & 0xFF),
                  static_cast<uint8_t>((out_id >> 16) & 0xFF),
                  static_cast<uint8_t>((out_id >> 24) & 0xFF),
                  flags,
                  txp,
                  name_ad);

    std::snprintf(tx_mac_, sizeof(tx_mac_), "010203040506");
}

uint8_t EF28FoxHuntView::response_flags_for(const EF28Device& peer) const {
    switch (response_flags_mode_) {
        case 1:
            return peer.flags;
        case 2:
            return 0x01 | 0x08;
        case 3:
            return 0x02;
        default:
            return (response_identity_ == 0) ? static_cast<uint8_t>(0x01 | 0x08) : static_cast<uint8_t>(0x02);
    }
}

bool EF28FoxHuntView::should_auto_reply_to(const EF28Device& peer) const {
    if (response_filter_ == 1) {
        return peer.type == 'D';
    }
    if (response_filter_ == 2) {
        return peer.type == 'B';
    }
    return true;
}

void EF28FoxHuntView::send_tx_burst_channel() {
    const uint8_t tx_channel = kAdvChannels[tx_burst_index_];
    transmitter_model.set_target_frequency(channel_to_freq(tx_channel));
    baseband::set_btletx(tx_channel, tx_mac_, tx_adv_, response_pdu_);
}

void EF28FoxHuntView::start_tx_response() {
    if (tx_active_) {
        return;
    }

    tx_active_ = true;
    tx_burst_index_ = 0;
    receiver_model.disable();
    baseband::shutdown();
    baseband::run_image(portapack::spi_flash::image_tag_btle_tx);
    transmitter_model.enable();
    send_tx_burst_channel();
    update_ui_text();
}

void EF28FoxHuntView::stop_tx_response() {
    if (!tx_active_) {
        return;
    }

    tx_active_ = false;
    transmitter_model.disable();
    baseband::shutdown();
    baseband::run_image(portapack::spi_flash::image_tag_btle_rx);
    baseband::set_btlerx(channel_number_);
    receiver_model.enable();
    tx_cooldown_ = 6;
    update_ui_text();
}

void EF28FoxHuntView::on_tx_progress(const bool done, uint32_t) {
    if (done && tx_active_) {
        if (tx_burst_index_ + 1 < kAdvChannels.size()) {
            ++tx_burst_index_;
            send_tx_burst_channel();
        } else {
            tx_done_count_++;
            stop_tx_response();
        }
    }
}

void EF28FoxHuntView::update_channel_hop() {
    baseband::set_btlerx(channel_number_);
    field_frequency.set_value(channel_to_freq(channel_number_));

    if (channel_number_ == 37) {
        channel_number_ = 38;
    } else if (channel_number_ == 38) {
        channel_number_ = 39;
    } else {
        channel_number_ = 37;
    }
}

void EF28FoxHuntView::on_timer() {
    if (tx_cooldown_ > 0) {
        --tx_cooldown_;
    }

    if (tx_pending_ && !tx_active_) {
        tx_pending_ = false;
        start_tx_response();
    }

    if (++hop_timer_count_ >= 6) {
        hop_timer_count_ = 0;
        if (!tx_active_) {
            update_channel_hop();
        }
    }

    if (++ui_timer_count_ >= 6) {
        ui_timer_count_ = 0;

        const int tracked_idx = tracked_device_index();

        if (tracked_idx >= 0) {
            auto& t = devices_[tracked_idx];

            if (tracked_rate_device_id_ == t.device_id) {
                tracked_pps_ = t.packet_count - tracked_last_packet_count_;
            } else {
                tracked_pps_ = 0;
            }

            tracked_rate_device_id_ = t.device_id;
            tracked_last_packet_count_ = t.packet_count;

            push_graph_samples();
        } else {
            tracked_pps_ = 0;
            tracked_rate_device_id_ = 0;
            tracked_last_packet_count_ = 0;
            push_graph_samples();
        }

        rebuild_sorted_indices();
        update_ui_text();
    }
}

}  // namespace ui
