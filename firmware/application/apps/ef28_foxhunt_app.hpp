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

#ifndef __EF28_FOXHUNT_APP_H__
#define __EF28_FOXHUNT_APP_H__

#include "ui.hpp"
#include "ui_navigation.hpp"
#include "ui_receiver.hpp"
#include "ui_freq_field.hpp"
#include "app_settings.hpp"
#include "radio_state.hpp"

#include "ch.h"

#include <array>
#include <cstdint>

namespace ui {

class EF28FoxHuntView : public View {
   public:
    EF28FoxHuntView(NavigationView& nav);
    ~EF28FoxHuntView();

    void focus() override;
    std::string title() const override { return "EF28 Fox Hunt"; }

   private:
    struct EF28Packet {
        uint32_t device_id{0};
        uint8_t type{0};
        uint8_t flags{0};
        int8_t tx_power{0};
    };

    struct EF28Device {
        bool used{false};
        uint32_t device_id{0};
        uint8_t type{0};
        uint8_t flags{0};
        int8_t tx_power{0};
        int16_t rssi{-127};
        int16_t ema_rssi{-127};
        systime_t last_seen_tick{0};
        uint32_t packet_count{0};
    };

    static constexpr uint16_t kMaxDevices = 32;
    static constexpr uint8_t kVisibleRows = 6;
    static constexpr uint8_t kManufacturerType = 0xFF;
    static constexpr uint16_t kCompanyId = 0x28EF;
    static constexpr uint8_t kVersion = 0x02;

    void on_data(BlePacketData* packet);
    void on_timer();

    bool parse_ef28_payload(const BlePacketData* packet, EF28Packet& out) const;
    int find_device(uint32_t device_id) const;
    int alloc_device(uint32_t device_id);
    int strongest_device_index() const;
    int tracked_device_index() const;
    void rebuild_sorted_indices();
    void update_ui_text();
    void push_graph_sample(int16_t rssi_now, int16_t rssi_ema);
    void update_channel_hop();
    uint64_t channel_to_freq(uint8_t channel) const;

    NavigationView& nav_;

    RxRadioState radio_state_{
        2402000000 /* frequency */,
        4000000 /* bandwidth */,
        4000000 /* sampling rate */,
        ReceiverModel::Mode::WidebandFMAudio};

    app_settings::SettingsManager settings_{
        "rx_ef28_foxhunt",
        app_settings::Mode::RX};

    uint8_t channel_number_{37};
    uint8_t hop_timer_count_{0};
    uint8_t ui_timer_count_{0};

    bool lock_enabled_{false};
    uint32_t locked_device_id_{0};
    uint8_t selected_rank_{0};

    uint32_t tracked_pps_{0};
    uint32_t tracked_last_packet_count_{0};
    uint32_t tracked_rate_device_id_{0};

    std::array<EF28Device, kMaxDevices> devices_{};
    std::array<uint8_t, kMaxDevices> sorted_indices_{};
    uint8_t sorted_count_{0};

    RxFrequencyField field_frequency{{UI_POS_X(0), UI_POS_Y(0)}, nav_};

    RFAmpField field_rf_amp{{UI_POS_X(13), UI_POS_Y(0)}};
    LNAGainField field_lna{{UI_POS_X(15), UI_POS_Y(0)}};
    VGAGainField field_vga{{UI_POS_X(18), UI_POS_Y(0)}};

    RSSI rssi{{UI_POS_X(21), 0, UI_POS_WIDTH_REMAINING(21), 4}};
    Channel channel{{UI_POS_X(21), 5, UI_POS_WIDTH_REMAINING(21), 4}};

    Button button_next{{0, 16, 8 * 8, 24}, "Next"};
    Button button_lock{{9 * 8, 16, 9 * 8, 24}, "Lock"};

    Text text_status{{19 * 8, 18, 13 * 8, 16}, "TRACK"};
    Text text_found{{0, 42, 15 * 8, 16}, "Found: 0"};
    Text text_target{{16 * 8, 42, 16 * 8, 16}, "Target: --"};

    Text text_current{{0, 58, 10 * 8, 16}, "RSSI: --"};
    Text text_ema{{10 * 8, 58, 12 * 8, 16}, "EMA: --"};
    Text text_pps{{22 * 8, 58, 10 * 8, 16}, "PPS: 0"};

    Text text_row_0{{0, 76, 32 * 8, 16}, ""};
    Text text_row_1{{0, 92, 32 * 8, 16}, ""};
    Text text_row_2{{0, 108, 32 * 8, 16}, ""};
    Text text_row_3{{0, 124, 32 * 8, 16}, ""};
    Text text_row_4{{0, 140, 32 * 8, 16}, ""};
    Text text_row_5{{0, 156, 32 * 8, 16}, ""};

    RSSIGraph rssi_graph{{0, 176, screen_width, screen_height - 176}};

    MessageHandlerRegistration message_handler_packet{
        Message::ID::BlePacket,
        [this](Message* const p) {
            const auto message = static_cast<const BLEPacketMessage*>(p);
            this->on_data(message->packet);
        }};

    MessageHandlerRegistration message_handler_frame_sync{
        Message::ID::DisplayFrameSync,
        [this](const Message* const) {
            this->on_timer();
        }};
};

}  // namespace ui

#endif  // __EF28_FOXHUNT_APP_H__
