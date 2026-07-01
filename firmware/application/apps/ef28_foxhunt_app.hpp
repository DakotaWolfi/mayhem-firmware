/*
 * Copyright (C) 2026
 * EF28 Fox Hunt RX-only cleanup.
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
        uint16_t seen_order{0};
    };

    static constexpr uint16_t kMaxDevices = 32;
    static constexpr uint8_t kVisibleRows = 4;
    static constexpr uint8_t kManufacturerType = 0xFF;
    static constexpr uint16_t kCompanyId = 0x28EF;
    static constexpr uint8_t kVersion = 0x02;

    void on_data(BlePacketData* packet);
    void on_timer();

    bool parse_ef28_payload(const BlePacketData* packet, EF28Packet& out) const;
    int find_device(uint32_t device_id) const;
    int find_device_by_mac(const uint8_t* mac) const;
    int alloc_device(uint32_t device_id);
    int tracked_device_index() const;
    void rebuild_sorted_indices();
    void update_ui_text();
    void set_config_page(bool enabled);
    void push_graph_sample(int16_t rssi_now, int16_t rssi_ema);
    bool device_is_fresh(const EF28Device& dev, systime_t now) const;
    std::string device_display_name(const EF28Device& dev) const;
    void update_channel_hop();
    void extract_local_name(const BlePacketData* packet, EF28Device& dev) const;
    uint64_t channel_to_freq(uint8_t channel) const;

    NavigationView& nav_;

    RxRadioState radio_state_{2402000000, 4000000, 4000000, ReceiverModel::Mode::WidebandFMAudio};
    app_settings::SettingsManager settings_{"rx_ef28_foxhunt", app_settings::Mode::RX};

    uint8_t channel_number_{37};
    uint8_t hop_timer_count_{0};
    uint8_t ui_timer_count_{0};

    bool lock_enabled_{false};
    uint32_t locked_device_id_{0};
    uint8_t selected_rank_{0};

    uint32_t tracked_pps_{0};
    uint32_t tracked_last_packet_count_{0};
    uint32_t tracked_rate_device_id_{0};

    bool config_page_{false};
    uint8_t info_mode_{0};
    uint8_t list_mode_{1};
    uint8_t sort_mode_{0};
    uint16_t next_seen_order_{1};

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
    Button button_page{{20 * 8, 16, 10 * 8, 24}, "Cfg"};

    OptionsField options_info_mode{{0, 82}, 7, {{"Info", 0}, {"MAC", 1}, {"Pkt", 2}}};
    OptionsField options_list_mode{{8 * 8, 82}, 7, {{"ID  ", 0}, {"Name", 1}}};
    OptionsField options_sort_mode{{16 * 8, 82}, 7, {{"Seen", 0}, {"RSSI", 1}}};

    Text text_cfg_title{{0, 50, 32 * 8, 16}, "EF28 RX V17 RESTORE"};
    Text text_cfg_hint{{0, 66, 32 * 8, 16}, "Info / list / sort only"};
    Text text_cfg_footer{{0, 114, 32 * 8, 16}, "TX removed for clean RX build"};

    Text text_status{{0, 50, 13 * 8, 16}, "RX TRACK"};
    Text text_found{{13 * 8, 50, 7 * 8, 16}, "F:0"};
    Text text_target{{20 * 8, 50, 12 * 8, 16}, "T:--"};
    Text text_current{{0, 66, 10 * 8, 16}, "RSSI:--"};
    Text text_ema{{10 * 8, 66, 12 * 8, 16}, "EMA:--"};
    Text text_pps{{22 * 8, 66, 10 * 8, 16}, "PPS:0"};
    Text text_name{{0, 82, 32 * 8, 16}, "Name:--"};
    Text text_info{{0, 98, 32 * 8, 16}, "No EF28 packets"};

    Text text_row_0{{0, 122, 32 * 8, 16}, ""};
    Text text_row_1{{0, 138, 32 * 8, 16}, ""};
    Text text_row_2{{0, 154, 32 * 8, 16}, ""};
    Text text_row_3{{0, 170, 32 * 8, 16}, ""};

    RSSIGraph rssi_graph{{0, 188, screen_width, screen_height - 188}};

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

#endif /* __EF28_FOXHUNT_APP_H__ */
