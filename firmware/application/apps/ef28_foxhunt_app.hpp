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
#include "ui_transmitter.hpp"
#include "ui_freq_field.hpp"
#include "ui_text_editor.hpp"
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
        uint8_t pdu_type{0};
        uint8_t flags{0};
        uint8_t mac[6]{};
        int8_t tx_power{0};
        bool has_name{false};
        std::array<char, 17> name{};
        int16_t rssi{-127};
        int16_t ema_rssi{-127};
        systime_t last_seen_tick{0};
        uint32_t packet_count{0};
    };

    static constexpr uint16_t kMaxDevices = 32;
    static constexpr uint8_t kVisibleRows = 5;
    static constexpr uint8_t kManufacturerType = 0xFF;
    static constexpr uint16_t kCompanyId = 0x28EF;
    static constexpr uint8_t kVersion = 0x02;
    static constexpr uint8_t kPktDiscovery = 2;  // matches the working BLE TX app DISCOVERY preset
    static constexpr uint8_t kPktAdvInd = 4;      // BLE TX app enum value, not raw BLE PDU nibble
    static constexpr uint8_t kPktAdvNonconn = 6;  // BLE TX app enum value, not raw BLE PDU nibble

    void on_data(BlePacketData* packet);
    void on_timer();
    void on_tx_progress(const bool done, uint32_t progress);

    bool parse_ef28_payload(const BlePacketData* packet, EF28Packet& out) const;
    int find_device(uint32_t device_id) const;
    int find_device_by_mac(const uint8_t* mac) const;
    int alloc_device(uint32_t device_id);
    int strongest_device_index() const;
    int tracked_device_index() const;
    void rebuild_sorted_indices();
    void update_ui_text();
    void set_config_page(bool enabled);
    void push_graph_samples();
    bool device_is_fresh(const EF28Device& dev, systime_t now) const;
    std::string device_display_name(const EF28Device& dev) const;
    void update_channel_hop();
    void queue_response_for(const EF28Device& peer);
    void start_tx_response();
    void send_tx_burst_channel();
    void stop_tx_response();
    void extract_local_name(const BlePacketData* packet, EF28Device& dev) const;
    uint8_t response_flags_for(const EF28Device& peer) const;
    bool should_auto_reply_to(const EF28Device& peer) const;
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

    bool tx_active_{false};
    bool tx_pending_{false};
    uint8_t tx_cooldown_{0};
    uint32_t tx_request_count_{0};
    uint32_t tx_done_count_{0};
    uint8_t tx_burst_index_{0};

    bool config_page_{false};

    uint8_t response_mode_{0};
    uint8_t response_identity_{0};
    uint8_t response_style_{0};
    uint8_t response_flags_mode_{0};
    uint8_t response_pdu_{kPktDiscovery};
    uint8_t response_filter_{0};
    uint8_t info_mode_{0};
    uint8_t list_mode_{1};
    uint8_t name_profile_{0};
    int8_t response_tx_power_{7};
    std::array<char, 18> custom_tx_name_{{'E','F','2','8','-','T','e','s','t','\0'}};
    uint32_t my_device_id_{0xEF280001};
    std::string custom_name_edit_buffer_{"EF28-Test"};

    char tx_mac_[13]{"EFAAAA000001"};
    char tx_adv_[63]{};

    std::array<EF28Device, kMaxDevices> devices_{};
    std::array<uint8_t, kMaxDevices> sorted_indices_{};
    uint8_t sorted_count_{0};

    RxFrequencyField field_frequency{{UI_POS_X(0), UI_POS_Y(0)}, nav_};

    RFAmpField field_rf_amp{{UI_POS_X(13), UI_POS_Y(0)}};
    LNAGainField field_lna{{UI_POS_X(15), UI_POS_Y(0)}};
    VGAGainField field_vga{{UI_POS_X(18), UI_POS_Y(0)}};

    RSSI rssi{{UI_POS_X(21), 0, UI_POS_WIDTH_REMAINING(21), 4}};
    Channel channel{{UI_POS_X(21), 5, UI_POS_WIDTH_REMAINING(21), 4}};

    Button button_next{{0, 16, 6 * 8, 24}, "Next"};
    Button button_lock{{7 * 8, 16, 8 * 8, 24}, "Lock"};
    Button button_reply{{16 * 8, 16, 11 * 8, 24}, "Send"};
    Button button_page{{28 * 8, 16, 4 * 8, 24}, "Cfg"};

    OptionsField options_response_mode{{0, 106},
                                       7,
                                       {{"AnsOff", 0},
                                        {"Manual", 1},
                                        {"Auto", 2}}};

    OptionsField options_identity{{8 * 8, 106},
                                  7,
                                  {{"Badge", 0},
                                   {"Beacon", 1}}};

    OptionsField options_style{{16 * 8, 106},
                               7,
                               {{"MyID", 0},
                                {"Mirror", 1}}};

    OptionsField options_tx_power{{24 * 8, 106},
                                  4,
                                  {{"-8", -8},
                                   {"0", 0},
                                   {"4", 4},
                                   {"7", 7}}};

    OptionsField options_display_mode{{0, 138},
                                      7,
                                      {{"Info", 0},
                                       {"MAC", 1},
                                       {"Pkt", 2},
                                       {"TX", 3}}};

    OptionsField options_list_mode{{8 * 8, 138},
                                   7,
                                   {{"ID", 0},
                                    {"Name", 1}}};

    OptionsField options_name_profile{{16 * 8, 138},
                                      7,
                                      {{"Test", 0},
                                       {"HackRF", 1},
                                       {"Fox", 2},
                                       {"Jenna", 3},
                                       {"Custom", 4}}};

    OptionsField options_flags_mode{{24 * 8, 138},
                                    7,
                                    {{"Auto", 0},
                                     {"Clone", 1},
                                     {"Mob", 2},
                                     {"Fix", 3}}};

    OptionsField options_reply_pdu{{0, 170},
                                   7,
                                   {{"DISC", kPktDiscovery},
                                    {"ADV", kPktAdvInd},
                                    {"NON", kPktAdvNonconn}}};

    OptionsField options_reply_filter{{8 * 8, 170},
                                      4,
                                      {{"All", 0},
                                       {"Bdg", 1},
                                       {"Bcn", 2}}};

    Text text_cfg_title{{0, 58, 32 * 8, 16}, "EF28 Config"};
    Text text_cfg_hint{{0, 74, 32 * 8, 16}, "Display rows and reply profile"};
    Text text_cfg_disp{{0, 90, 32 * 8, 16}, "Reply mode / role / id / TX power"};
    Text text_cfg_reply{{0, 122, 32 * 8, 16}, "Info / list / TX name / flags"};
    Text text_cfg_name{{0, 154, 32 * 8, 16}, "TX name: EF28-Test"};
    Button button_edit_name{{20 * 8, 170, 12 * 8, 24}, "Edit name"};
    Text text_cfg_footer{{0, 210, 32 * 8, 16}, "DISC TX matches BLE TX preset"};

    Text text_status{{0, 58, 13 * 8, 16}, "TRACK"};
    Text text_found{{13 * 8, 58, 9 * 8, 16}, "F:0"};
    Text text_target{{22 * 8, 58, 10 * 8, 16}, "T:--"};

    Text text_current{{0, 74, 10 * 8, 16}, "RSSI: --"};
    Text text_ema{{10 * 8, 74, 12 * 8, 16}, "EMA: --"};
    Text text_pps{{22 * 8, 74, 10 * 8, 16}, "PPS: 0"};

    Text text_name{{0, 90, 32 * 8, 16}, "Name: --"};
    Text text_info{{0, 106, 32 * 8, 16}, "MAC: --"};
    Text text_tx{{0, 42, 32 * 8, 16}, "TX: idle"};

    Text text_row_0{{0, 122, 32 * 8, 16}, ""};
    Text text_row_1{{0, 138, 32 * 8, 16}, ""};
    Text text_row_2{{0, 154, 32 * 8, 16}, ""};
    Text text_row_3{{0, 170, 32 * 8, 16}, ""};
    Text text_row_4{{0, 186, 32 * 8, 16}, ""};

    RSSIGraph rssi_graph{{0, 198, screen_width, screen_height - 198}};

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

    MessageHandlerRegistration message_handler_tx_progress{
        Message::ID::TXProgress,
        [this](const Message* const p) {
            const auto message = *reinterpret_cast<const TXProgressMessage*>(p);
            this->on_tx_progress(message.done, message.progress);
        }};
};

}  // namespace ui

#endif  // __EF28_FOXHUNT_APP_H__
