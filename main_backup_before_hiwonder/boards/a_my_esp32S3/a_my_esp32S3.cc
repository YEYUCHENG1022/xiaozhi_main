#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include "power_save_timer.h"
// #include "../xingzhi-cube-1.54tft-wifi/power_manager.h"


#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

// 电池adc采集头文件 实现定时周期查看电池电量(BAT和USB)
// #include "../aipi-lite/power_manager.h"
#include "my_adc_read.h"
#include <esp_adc/adc_oneshot.h>

// 步进电机头文件
#include "motor_drive.h"

#define TAG "A_MY_ESP32S3"

class A_MY_ESP32S3 : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;// 显示屏的 I2C 总线句柄
    Button boot_button_; // 启动按键（GPIO 由配置文件定义）
    Display* display_;                            // 显示对象（OLED）
    
    PowerManager* power_manager_;                  // 电源管理器（电池/电源状态）
    PowerSaveTimer* power_save_timer_;            // 电源省电定时器（定时关闭屏幕）
    esp_lcd_panel_io_handle_t panel_io_ = nullptr; // LCD 面板 I/O 句柄
    esp_lcd_panel_handle_t panel_ = nullptr;       // LCD 面板句柄

    MotorDrive* motor_drive_; // 步进电机驱动对象



    // I2C初始化
    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_I2C_GPIO_SDA,
            .scl_io_num = DISPLAY_I2C_GPIO_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }
    // oled初始化
    void InitializeSsd1306Display() {
        // --- 1. 配置 I2C 通信接口 ---
        // SSD1306 通常通过 I2C 协议通信，这里定义了 IO 配置结构体
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,              // SSD1306 的 I2C 7位设备地址 (0x3C 或 0x3D)
            .on_color_trans_done = nullptr, // 颜色传输完成的回调函数，此处不使用
            .user_ctx = nullptr,           // 用户自定义上下文，此处不使用
            .control_phase_bytes = 1,      // 控制阶段字节数
            .dc_bit_offset = 6,            // DC (Data/Command) 位偏移量，用于区分指令和数据
            .lcd_cmd_bits = 8,             // 发送命令的位宽 (8位)
            .lcd_param_bits = 8,           // 发送参数的位宽 (8位)
            .flags = {
                .dc_low_on_data = 0,       // 数据传输时 DC 电平配置
                .disable_control_phase = 0,// 是否禁用控制阶段
            },
            .scl_speed_hz = 400 * 1000,    // I2C SCL 时钟频率：400kHz (快速模式)
        };

        // --- 2. 创建 LCD 面板 IO 句柄 ---
        // 使用之前配置好的 I2C 总线 和 io_config 创建 IO 句柄 panel_io_
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        // --- 3. 配置 SSD1306 驱动参数 ---
        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        
        panel_config.reset_gpio_num = -1;   // 复位引脚号，-1 表示不使用硬件复位引脚
        panel_config.bits_per_pixel = 1;    // 色深：1位 (单色显示屏，只有亮/灭)

        // SSD1306 特定的配置结构体
        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT), // 设置屏幕高度 (如 32, 64, 128)
        };
        panel_config.vendor_config = &ssd1306_config; // 将特定配置挂载到通用配置中

        // --- 4. 安装 SSD1306 驱动程序 ---
        // 根据配置创建 SSD1306 驱动句柄 panel_
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // --- 5. 复位并初始化屏幕 ---
        // 对屏幕进行软复位
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        
        // 初始化屏幕内部寄存器
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            // 如果初始化失败（例如硬件未连接），记录错误日志
            ESP_LOGE(TAG, "Failed to initialize display");
            
            // 降级处理：实例化一个 NoDisplay 对象，防止后续代码调用空指针导致崩溃
            display_ = new NoDisplay();
            return;
        }

        // --- 6. 打开显示 ---
        // 发送指令开启显示屏（此时屏幕可能还是全黑，直到写入数据）
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        // --- 7. 实例化应用层显示对象 ---
        // 创建 OledDisplay 对象，封装了底层的 panel_io_ 和 panel_，并提供绘图接口
        // 参数包括：宽、高、X轴镜像、Y轴镜像（用于调整显示方向）
        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    // 按钮初始化 //需要优化
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            // 获取程序单例
            auto& app = Application::GetInstance();
            // 获取当前设备的状态
            if (app.GetDeviceState() == kDeviceStateStarting) {
                // 处于开启状态， 则进入wifi配置状态
                EnterWifiConfigMode();
                return;
            }
            //  如果设备已启动，则切换聊天状态 
            app.ToggleChatState();
        });
    }

    // 电池管理器注册 获取电池的电量
    void InitializePowerManager() {
        //只需要定时检测电池带能量即可
        power_manager_ = new PowerManager(BATTERY_GPIO_ADC_BAT,ADC_CHANNEL_7);
        //可不需要
        // power_manager_->OnChargingStatusChanged([this](bool is_charging) {
        //     if (is_charging) {
        //         power_save_timer_->SetEnabled(false);
        //     } else {
        //         power_save_timer_->SetEnabled(true);
        //     }
        // });
    }

    
    
    // 电机驱动初始化
    void InitializeMotorDriver() {
        motor_drive_ = new MotorDrive(MOTOR_GPIO_SHCP, MOTOR_GPIO_STCP, MOTOR_GPIO_DS,2);
        // MotorDrive(gpio_num_t sck, gpio_num_t rck, gpio_num_t sda, uint8_t level = 2)
    }
    void TestMotor() {
    motor_drive_->ForwardSteps(200, 5);

    vTaskDelay(pdMS_TO_TICKS(1000));

    motor_drive_->BackwardSteps(200, 5);
    }
# if 0 
    // Camera 初始化 和oled 使用同样一个iic 先不用
    void InitializeCamera() {
        static esp_cam_ctlr_dvp_pin_config_t dvp_pin_config = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io = {
                [0] = CAMERA_GPIO_CAM_D0,
                [1] = CAMERA_GPIO_CAM_D1,
                [2] = CAMERA_GPIO_CAM_D2,
                [3] = CAMERA_GPIO_CAM_D3,
                [4] = CAMERA_GPIO_CAM_D4,
                [5] = CAMERA_GPIO_CAM_D5,
                [6] = CAMERA_GPIO_CAM_D6,
                [7] = CAMERA_GPIO_CAM_D7,
            },
            .vsync_io = CAMERA_GPIO_CAM_VSYNC,
            .de_io = CAMERA_GPIO_CAM_HREF,
            .pclk_io = CAMERA_GPIO_CAM_PCLK,
            .xclk_io = CAMERA_GPIO_CAM_XCLK,
        };

        esp_video_init_sccb_config_t sccb_config = {
            .init_sccb = true,
            .i2c_config = {
                .port = 1,
                .scl_pin = CAMERA_PIN_SIOC,
                .sda_pin = CAMERA_PIN_SIOD,
            },
            .freq = 100000,
        };

        esp_video_init_dvp_config_t dvp_config = {
            .sccb_config = sccb_config,
            .reset_pin = CAMERA_PIN_RESET,
            .pwdn_pin = CAMERA_GPIO_CAM_PWDN,
            .dvp_pin = dvp_pin_config,
            .xclk_freq = XCLK_FREQ_HZ,
        };

        esp_video_init_config_t video_config = {
            .dvp = &dvp_config,
        };

        camera_ = new EspVideo(video_config);

#if ( CONFIG_CAMERA_OV3660 )
            camera_->SetVFlip(true);
            camera_->SetHMirror(true);
#elif ( CONFIG_CAMERA_OV2640 )
            camera_->SetVFlip(false);
            camera_->SetHMirror(false);
#endif
    }
#endif

    // MCP Tools 初始化
    void InitializeTools() {
        // 参考 MCP 文档
    }

public:
    // 构造函数
    A_MY_ESP32S3() : 
    boot_button_(BOOT_BUTTON_GPIO) {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        // InitializePowerManager();
        InitializeMotorDriver();
        TestMotor();
        
    }
    // led指示灯 x
    virtual Led* GetLed() override {
        static SingleLed led(LED_GPIO_LED1);
        return &led;
    }


    // 需要接入电机控制

    // 需要加入adc 采集实现电池电量显示
    // 需要加入 摄像头控制 ov2640

    // 获取音频编解码器
    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_SPK_GPIO_BCK, 
            AUDIO_SPK_GPIO_LCK, 
            AUDIO_SPK_GPIO_DOUT,
            AUDIO_MIC_GPIO_SCK,
            AUDIO_MIC_GPIO_WS, 
            AUDIO_MIC_GPIO_SD
        );
        return &audio_codec;
    }

    // 获取显示屏
    virtual Display* GetDisplay() override {
        return display_;
    }
    
    // // 获取背光控制
    // virtual Backlight* GetBacklight() override {
    //     static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
    //     return &backlight;
    // }
};

// 注册开发板
DECLARE_BOARD(A_MY_ESP32S3);