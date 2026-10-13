#pragma once  // 防止头文件重复包含

#include <vector>
#include <functional>

#include <esp_timer.h>
#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>

/**
 * @brief 电源管理类，负责检测充电状态、读取电池电量、监控低电量状态
 * 
 * 通过 GPIO 引脚检测充电器是否插入，通过 ADC 采样电池电压，经过平均滤波后查表得到电量百分比。
 * 使用定时器定期执行检查，当充电状态或低电量状态变化时调用注册的回调函数。
 */
class PowerManager {
private:
    esp_timer_handle_t timer_handle_;                     // 定时器句柄，用于周期性检查电池状态
    std::function<void(bool)> on_charging_status_changed_;   // 充电状态变化回调函数（参数：是否充电中）
    std::function<void(bool)> on_low_battery_status_changed_; // 低电量状态变化回调函数（参数：是否低电量）

    gpio_num_t charging_pin_ = GPIO_NUM_NC;               // 充电检测引脚，默认未连接
    adc_channel_t adc_channel_ = ADC_CHANNEL_7;             // ADC 通道，默认为通道7
    std::vector<uint16_t> adc_values_;                     // 存储最近几次 ADC 采样值，用于平均滤波
    uint32_t battery_level_ = 0;                            // 当前电池电量百分比（0～100）
    bool is_charging_ = false;                              // 当前充电状态（true=正在充电）
    bool is_low_battery_ = false;                           // 当前低电量状态（true=电量过低）
    int ticks_ = 0;                                          // 定时器触发计数，用于控制 ADC 采样间隔
    const int kBatteryAdcInterval = 60;                     // ADC 采样间隔（每 60 次定时器触发采样一次）
    const int kBatteryAdcDataCount = 3;                     // 用于平均滤波的 ADC 采样次数
    const int kLowBatteryLevel = 20;                         // 低电量阈值（百分比）

    adc_oneshot_unit_handle_t adc_handle_;                  // ADC 单次转换单元句柄

    /**
     * @brief 定时检查电池状态（由定时器周期性调用）
     * 
     * 1. 读取充电引脚电平，若充电状态发生变化则更新并触发回调，同时立即读取一次 ADC。
     * 2. 若 ADC 数据不足，则读取一次。
     * 3. 否则按间隔计数（kBatteryAdcInterval）定期读取 ADC。
     */
    void CheckBatteryStatus() {
        // 获取当前充电状态（引脚高电平表示充电中？具体电平含义由硬件决定）
        bool new_charging_status = gpio_get_level(charging_pin_) == 1;
        if (new_charging_status != is_charging_) {
            is_charging_ = new_charging_status;
            if (on_charging_status_changed_) {
                on_charging_status_changed_(is_charging_);  // 触发充电状态回调
            }
            ReadBatteryAdcData();  // 状态变化时立即读取一次 ADC
            return;
        }

        // 如果电池电量数据不足（少于目标采样数），则读取电池电量数据
        if (adc_values_.size() < kBatteryAdcDataCount) {
            ReadBatteryAdcData();
            return;
        }

        // 数据充足时，每 kBatteryAdcInterval 次 tick 读取一次
        ticks_++;
        if (ticks_ % kBatteryAdcInterval == 0) {
            ReadBatteryAdcData();
        }
    }

    /**
     * @brief 读取一次电池 ADC 值，更新平均电量并检查低电量状态
     * 
     * 执行 ADC 单次采样，将原始值存入队列，计算平均值后通过查表插值得到电量百分比。
     * 当采样数达到要求后，判断低电量状态，如有变化触发回调。
     */
    void ReadBatteryAdcData() {
        int adc_value;
        // 从 ADC 通道 6 读取原始值（ADC_UNIT_2, ADC_CHANNEL_7 已在构造函数中配置）
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle_, adc_channel_, &adc_value));
        
        // 将 ADC 值添加到队列末尾
        adc_values_.push_back(adc_value);
        // 如果队列超过目标长度，移除最旧的值
        if (adc_values_.size() > kBatteryAdcDataCount) {
            adc_values_.erase(adc_values_.begin());
        }
        // 计算平均值
        uint32_t average_adc = 0;
        for (auto value : adc_values_) {
            average_adc += value;
        }
        average_adc /= adc_values_.size();

        // 定义电池电量与 ADC 值的映射表（根据实际硬件标定）
        //  vef 为3.3V ，测量电压值为3~4.2v,由于分压电路限制为1.5V~2.1V
        const struct {
            uint16_t adc;   // ADC 原始值
            uint8_t level;  // 对应电量百分比
        } levels[] = {
            {1862, 0},   // 3.0V/2 （放电截止电压）
            {2172, 20},  // 3.5V/2
            {2296, 40},  // 3.7V/2
            {2359, 60},  // 3.8V/2
            {2482, 80},  // 4.0V/2
            {2607, 100}  // 4.2V/2 （满电）
        };

        // 低于最低值时，电量为 0%
        if (average_adc < levels[0].adc) {
            battery_level_ = 0;
        }
        // 高于最高值时，电量为 100%
        else if (average_adc >= levels[5].adc) {
            battery_level_ = 100;
        } else {
            // 在线性区间内插值计算电量百分比 获取电池电量
            for (int i = 0; i < 5; i++) {
                if (average_adc >= levels[i].adc && average_adc < levels[i+1].adc) {
                    float ratio = static_cast<float>(average_adc - levels[i].adc) / 
                                   (levels[i+1].adc - levels[i].adc);
                    battery_level_ = levels[i].level + 
                                     ratio * (levels[i+1].level - levels[i].level);
                    break;
                }
            }
        }

        // 当已采集足够的数据后，检查低电量状态
        if (adc_values_.size() >= kBatteryAdcDataCount) {
            bool new_low_battery_status = battery_level_ <= kLowBatteryLevel;
            if (new_low_battery_status != is_low_battery_) {
                is_low_battery_ = new_low_battery_status;
                if (on_low_battery_status_changed_) {
                    on_low_battery_status_changed_(is_low_battery_); // 触发低电量回调
                }
            }
        }

        // 打印调试信息：原始 ADC 值、平均值、当前电量
        ESP_LOGI("PowerManager", "ADC value: %d average: %ld level: %ld", 
                 adc_value, average_adc, battery_level_);
    }

public:
    /**
     * @brief 构造函数
     * @param pin 充电检测引脚（GPIO 编号），若未使用可传入 GPIO_NUM_NC 
     * 默认为adc1  通道可自定义 
     * 
     * 初始化充电检测引脚为输入模式，创建周期性定时器（周期 1 秒），并初始化 ADC 单元及通道。
     */
    PowerManager(gpio_num_t pin,adc_channel_t adc_channel) :
     charging_pin_(pin),adc_channel_(adc_channel)  {

        // 配置充电检测引脚为输入模式，无上下拉
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;      // 禁止中断
        io_conf.mode = GPIO_MODE_INPUT;              // 输入模式
        io_conf.pin_bit_mask = (1ULL << charging_pin_); // 引脚掩码
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; // 禁止下拉
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;     // 禁止上拉
        gpio_config(&io_conf);

        // 创建电池电量检查定时器
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                PowerManager* self = static_cast<PowerManager*>(arg);
                self->CheckBatteryStatus();        // 定时调用检查函数
            },
            .arg = this,                               // 传递 this 指针
            .dispatch_method = ESP_TIMER_TASK,         // 在定时器任务中执行
            .name = "battery_check_timer",             // 定时器名称
            .skip_unhandled_events = true,             // 跳过未处理事件
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle_));
        // 启动周期性定时器，周期 1 秒 (1000000 微秒)
        ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle_, 1000000));

        // 初始化 ADC 单元（使用 ADC1）
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_1,                     // 使用 ADC1
            .ulp_mode = ADC_ULP_MODE_DISABLE,          // 禁止 ULP 模式
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle_));
        
        // 配置 ADC 通道（ADC1 通道 7）
        adc_oneshot_chan_cfg_t chan_config = {
            .atten = ADC_ATTEN_DB_12,                  // 衰减 12dB，测量范围约 0~3.9V
            .bitwidth = ADC_BITWIDTH_12,                // 12 位分辨率（0-4095）
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle_, adc_channel_, &chan_config));
    }

    /**
     * @brief 析构函数：停止并删除定时器，释放 ADC 单元
     */
    ~PowerManager() {
        if (timer_handle_) {
            esp_timer_stop(timer_handle_);
            esp_timer_delete(timer_handle_);
        }
        if (adc_handle_) {
            adc_oneshot_del_unit(adc_handle_);
        }
    }

    /**
     * @brief 判断是否处于充电状态（满电时不视为充电）
     * @return true 正在充电且电量未满，false 未充电或已满
     */
    bool IsCharging() {
        // 如果电量已经满了，则不再显示充电中
        if (battery_level_ == 100) {
            return false;
        }
        return is_charging_;
    }

    /**
     * @brief 判断是否处于放电状态（与充电状态相反）
     * @return true 放电中，false 充电中
     * 
     * 注意：未区分放电与充满断电，直接返回 !is_charging_。
     */
    bool IsDischarging() {
        // 没有区分充电和放电，所以直接返回相反状态
        return !is_charging_;
    }

    /**
     * @brief 获取当前电池电量百分比
     * @return 电量值 0-100
     */
    uint8_t GetBatteryLevel() {
        return battery_level_;
    }

    /**
     * @brief 注册低电量状态变化回调
     * @param callback 回调函数，参数为 bool 表示是否低电量
     */
    void OnLowBatteryStatusChanged(std::function<void(bool)> callback) {
        on_low_battery_status_changed_ = callback;
    }

    /**
     * @brief 注册充电状态变化回调
     * @param callback 回调函数，参数为 bool 表示是否正在充电
     */
    void OnChargingStatusChanged(std::function<void(bool)> callback) {
        on_charging_status_changed_ = callback;
    }
};