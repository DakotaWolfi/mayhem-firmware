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

using namespace portapack;

namespace ui {

namespace {

constexpr int16_t kRssiFloor = -100;
constexpr int16_t kRssiCeil = -30;
constexpr float kEmaAlpha = 0.30f;

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
                  &text_status,
                  &text_found,
                  &text_target,
                  &text_current,
                  &text_ema,
                  &text_pps,
                  &text_row_0,
                  &text_row_1,
                  &text_row_2,
                  &text_row_3,
                  &text_row_4,
                  &text_row_5,
                  &rssi_graph});

    field_frequency.set_step(0);
    field_frequency.set_value(channel_to_freq(channel_number_));

    rssi_graph.set_nb_columns(120);

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

    baseband::set_btlerx(channel_number_);
    receiver_model.enable();
}

EF28FoxHuntView::~EF28FoxHuntView() {
    receiver_model.disable();
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
    EF28Packet parsed{};
    if (!parse_ef28_payload(packet, parsed)) {
        return;
    }

    const systime_t now = chTimeNow();
    int idx = find_device(parsed.device_id);
    if (idx < 0) {
        idx = alloc_device(parsed.device_id);
    }

    auto& dev = devices_[idx];

    const int16_t corrected_rssi = packet->max_dB - (receiver_model.lna() + receiver_model.vga() + (receiver_model.rf_amp() ? 14 : 0));

    dev.type = parsed.type;
    dev.flags = parsed.flags;
    dev.tx_power = parsed.tx_power;
    dev.rssi = corrected_rssi;

    if (dev.ema_rssi == -127) {
        dev.ema_rssi = corrected_rssi;
    } else {
        dev.ema_rssi = static_cast<int16_t>(kEmaAlpha * corrected_rssi + (1.0f - kEmaAlpha) * dev.ema_rssi);
    }

    dev.last_seen_tick = now;
    dev.packet_count++;

    rebuild_sorted_indices();
}

void EF28FoxHuntView::rebuild_sorted_indices() {
    sorted_count_ = 0;

    for (uint8_t i = 0; i < kMaxDevices; ++i) {
        if (devices_[i].used) {
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

void EF28FoxHuntView::push_graph_sample(int16_t rssi_now, int16_t rssi_ema) {
    if (rssi_now < kRssiFloor) rssi_now = kRssiFloor;
    if (rssi_now > kRssiCeil) rssi_now = kRssiCeil;

    if (rssi_ema < kRssiFloor) rssi_ema = kRssiFloor;
    if (rssi_ema > kRssiCeil) rssi_ema = kRssiCeil;

    rssi_graph.add_values(rssi_now, rssi_ema, rssi_now, rssi_now);
}

void EF28FoxHuntView::update_ui_text() {
    const int tracked_idx = tracked_device_index();

    text_status.set(lock_enabled_ ? "LOCK" : "TRACK");
    text_found.set("Found: " + to_string_dec_uint(sorted_count_));

    if (tracked_idx >= 0) {
        const auto& t = devices_[tracked_idx];
        text_target.set("Target: " + to_string_hex(t.device_id, 8));
        text_current.set("RSSI: " + to_string_dec_int(t.rssi));
        text_ema.set("EMA: " + to_string_dec_int(t.ema_rssi));
        text_pps.set("PPS: " + to_string_dec_uint(tracked_pps_));
    } else {
        text_target.set("Target: --");
        text_current.set("RSSI: --");
        text_ema.set("EMA: --");
        text_pps.set("PPS: 0");
    }

    std::array<Text*, kVisibleRows> rows{{&text_row_0, &text_row_1, &text_row_2, &text_row_3, &text_row_4, &text_row_5}};

    for (uint8_t i = 0; i < kVisibleRows; ++i) {
        if (i >= sorted_count_) {
            rows[i]->set("");
            continue;
        }

        const uint8_t idx = sorted_indices_[i];
        const auto& d = devices_[idx];
        const uint32_t age_s = static_cast<uint32_t>((chTimeNow() - d.last_seen_tick) / CH_FREQUENCY);
        const bool selected = (i == selected_rank_);

        std::string line = selected ? "> " : "  ";
        line += to_string_hex(d.device_id, 8);
        line += " ";
        line += (d.type == 'D') ? "D" : ((d.type == 'B') ? "B" : "?");
        line += " ";
        line += to_string_dec_int(d.rssi);
        line += "dB ";
        line += to_string_dec_uint(d.packet_count);
        line += " ";
        line += to_string_dec_uint(age_s);
        line += "s";

        rows[i]->set(line);
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
    if (++hop_timer_count_ >= 6) {
        hop_timer_count_ = 0;
        update_channel_hop();
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

            push_graph_sample(t.rssi, t.ema_rssi);
        } else {
            tracked_pps_ = 0;
            tracked_rate_device_id_ = 0;
            tracked_last_packet_count_ = 0;
            push_graph_sample(kRssiFloor, kRssiFloor);
        }

        update_ui_text();
    }
}

}  // namespace ui
