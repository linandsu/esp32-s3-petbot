#include "gree_ir_encoder.h"
#include <vector>
#include "driver/rmt_tx.h"
#include "esp_log.h"

#define TAG "GreeIR"

namespace iot {

void GreeIrEncoder::SendCommand(int gpio_num, bool power, Mode mode, int temp, FanSpeed fan) {
    if (temp < 16) temp = 16;
    if (temp > 30) temp = 30;

    // 1. Pack the 8 bytes for Gree YAN1F1 protocol
    uint8_t data[8] = {0};
    
    // Byte 0: Mode (0..2), Power (3), Fan (4..5), Swing (6), Sleep (7)
    data[0] = (mode & 0x07) | ((power ? 1 : 0) << 3) | ((fan & 0x03) << 4);
    
    // Byte 1: Temp (0..3)
    data[1] = (temp - 16) & 0x0F;
    
    // Fixed bytes for standard YAN1F1
    data[2] = 0x20;
    data[3] = 0x50;
    data[4] = 0x02;
    data[5] = 0x00;
    data[6] = 0x20;
    
    // Calculate checksum
    int sum = (data[0] & 0x0F) + (data[1] & 0x0F) + (data[2] & 0x0F) + 
              (data[3] & 0x0F) + ((data[5] & 0xF0) >> 4) + (data[6] & 0x0F) + 0x0A;
    data[7] = ((sum & 0x0F) << 4);

    // 2. Setup RMT TX channel
    rmt_tx_channel_config_t tx_channel_cfg = {};
    tx_channel_cfg.gpio_num = (gpio_num_t)gpio_num;
    tx_channel_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_channel_cfg.resolution_hz = 1000000; // 1MHz, 1us tick
    tx_channel_cfg.mem_block_symbols = 64;
    tx_channel_cfg.trans_queue_depth = 4;

    rmt_channel_handle_t tx_channel = NULL;
    esp_err_t err = rmt_new_tx_channel(&tx_channel_cfg, &tx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(err));
        return;
    }

    rmt_carrier_config_t carrier_cfg = {};
    carrier_cfg.duty_cycle = 0.33;
    carrier_cfg.frequency_hz = 38000; // 38kHz IR carrier
    carrier_cfg.flags.polarity_active_low = false;
    ESP_ERROR_CHECK(rmt_apply_carrier(tx_channel, &carrier_cfg));

    // 3. Build symbols
    std::vector<rmt_symbol_word_t> symbols;
    
    auto add_mark_space = [&](int mark, int space) {
        rmt_symbol_word_t sym;
        sym.val = 0;
        sym.duration0 = mark;  sym.level0 = 1;
        sym.duration1 = space; sym.level1 = 0;
        symbols.push_back(sym);
    };

    auto add_byte = [&](uint8_t byte) {
        for (int i = 0; i < 8; i++) {
            if (byte & (1 << i)) add_mark_space(620, 1600); // Logic 1
            else add_mark_space(620, 540);                  // Logic 0
        }
    };

    // Header
    add_mark_space(9000, 4500);
    
    // 35 bits: Byte 0, 1, 2, 3, and first 3 bits of Byte 4
    add_byte(data[0]);
    add_byte(data[1]);
    add_byte(data[2]);
    add_byte(data[3]);
    for (int i = 0; i < 3; i++) {
        if (data[4] & (1 << i)) add_mark_space(620, 1600);
        else add_mark_space(620, 540);
    }
    
    // Gap
    add_mark_space(620, 20000); // 20ms gap

    // Remaining 29 bits: last 5 bits of Byte 4, then Byte 5, 6, 7
    for (int i = 3; i < 8; i++) {
        if (data[4] & (1 << i)) add_mark_space(620, 1600);
        else add_mark_space(620, 540);
    }
    add_byte(data[5]);
    add_byte(data[6]);
    add_byte(data[7]);

    // Footer
    add_mark_space(620, 40000);

    // 4. Transmit
    rmt_copy_encoder_config_t copy_encoder_config = {};
    rmt_encoder_handle_t copy_encoder = NULL;
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_config, &copy_encoder));

    rmt_transmit_config_t transmit_config = {};
    transmit_config.loop_count = 0;

    ESP_LOGI(TAG, "Transmitting Gree IR Code...");
    ESP_ERROR_CHECK(rmt_enable(tx_channel));
    ESP_ERROR_CHECK(rmt_transmit(tx_channel, copy_encoder, symbols.data(), symbols.size() * sizeof(rmt_symbol_word_t), &transmit_config));
    rmt_tx_wait_all_done(tx_channel, -1);
    
    rmt_disable(tx_channel);
    rmt_del_encoder(copy_encoder);
    rmt_del_channel(tx_channel);
}

} // namespace iot
