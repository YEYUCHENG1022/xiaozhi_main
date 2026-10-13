#pragma once

#include <stdint.h>
#include <string.h>
#include <vector>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"


class MotorDrive {
private:
    gpio_num_t sck_pin_;
    gpio_num_t rck_pin_;
    gpio_num_t sda_pin_;
    uint8_t level_;

    inline void SCK_Low()  { gpio_set_level(sck_pin_, 0); }
    inline void SCK_High() { gpio_set_level(sck_pin_, 1); }

    inline void RCK_Low()  { gpio_set_level(rck_pin_, 0); }
    inline void RCK_High() { gpio_set_level(rck_pin_, 1); }

    inline void Data_Low()  { gpio_set_level(sda_pin_, 0); }
    inline void Data_High() { gpio_set_level(sda_pin_, 1); }

public:
    MotorDrive(gpio_num_t sck, gpio_num_t rck, gpio_num_t sda, uint8_t level = 2)
        : sck_pin_(sck), rck_pin_(rck), sda_pin_(sda), level_(level)
    {
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask =
            (1ULL << sck_pin_) |
            (1ULL << rck_pin_) |
            (1ULL << sda_pin_);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

        gpio_config(&io_conf);

        SCK_Low();
        RCK_Low();
        Data_Low();
    }

    ~MotorDrive() {
        gpio_reset_pin(sck_pin_);
        gpio_reset_pin(rck_pin_);
        gpio_reset_pin(sda_pin_);
    }

    void Save() {
        RCK_Low();
        esp_rom_delay_us(10);   
        RCK_High();
    }

    void SendByte(uint8_t byte) {
        for (int i = 0; i < 8; i++) {
            SCK_Low();

            if (byte & 0x80) {
                Data_High();
            } else {
                Data_Low();
            }

            byte <<= 1;
            SCK_High();
        }
    }

    void SendBuffer(uint8_t* data, uint16_t len) {
        for (int i = 0; i < len; i++) {
            SendByte(data[i]);
        }
        Save();
    }

    void Test() {
        uint8_t ele[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

        while (true) {
            int max_num = level_ * 8;

            for (int i = 1; i <= max_num; i++) {
                vTaskDelay(pdMS_TO_TICKS(300));

                std::vector<uint8_t> buf(level_, 0);

                int floor = (i - 1) / 8;
                int place = (i % 8 == 0) ? 7 : (i % 8 - 1);

                buf[level_ - floor - 1] = ele[7 - place];

                SendBuffer(buf.data(), level_);
            }
        }
    }
    void ForwardSteps(int steps, int delay_ms = 5);
    void BackwardSteps(int steps, int delay_ms = 5);
    void Stop();
};