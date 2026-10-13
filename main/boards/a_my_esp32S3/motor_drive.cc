#include "motor_drive.h"

static const char* TAG_MOTOR = "MotorDrive";

static const uint8_t step_seq[4] = {
    0x01,
    0x02,
    0x04,
    0x08
};

void MotorDrive::ForwardSteps(int steps, int delay_ms) {
    ESP_LOGI(TAG_MOTOR, "ForwardSteps: %d", steps);

    for (int i = 0; i < steps; i++) {
        uint8_t value = step_seq[i % 4];

        std::vector<uint8_t> buf(level_, 0);

        // 默认输出到最后一个 74HC595 的低4位
        buf[level_ - 1] = value;

        SendBuffer(buf.data(), level_);

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    Stop();
}

void MotorDrive::BackwardSteps(int steps, int delay_ms) {
    ESP_LOGI(TAG_MOTOR, "BackwardSteps: %d", steps);

    for (int i = 0; i < steps; i++) {
        uint8_t value = step_seq[(4 - (i % 4)) % 4];

        std::vector<uint8_t> buf(level_, 0);

        // 默认输出到最后一个 74HC595 的低4位
        buf[level_ - 1] = value;

        SendBuffer(buf.data(), level_);

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    Stop();
}

void MotorDrive::Stop() {
    std::vector<uint8_t> buf(level_, 0);
    SendBuffer(buf.data(), level_);
}