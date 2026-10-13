#ifndef _IO_CONFIG_H_
#define _IO_CONFIG_H_

#include <driver/gpio.h>

/**
 * @brief ESP32S3 引脚定义文件
 * 包含所有外设的GPIO配置，根据硬件引脚连接表配置
 */

// ============================================================================
// 音频接口 - 数字麦克风 (INMP441/ICS-43434 I2S接口)
// ============================================================================
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define RESET_GPIO_NUM      GPIO_NUM_NC


#define AUDIO_MIC_GPIO_WS       GPIO_NUM_14    // WS 数据选择
#define AUDIO_MIC_GPIO_SCK      GPIO_NUM_12    // SCK 数据时钟
#define AUDIO_MIC_GPIO_SD       GPIO_NUM_13    // SD 数据输入
// VDD 3V3, GND 接地, L/R 短接左/右声道

// ============================================================================
// 音频接口 - 数字功放 (NS4168)
// ============================================================================
#define AUDIO_SPK_GPIO_LCK      GPIO_NUM_9     // SPK_LCK
#define AUDIO_SPK_GPIO_BCK      GPIO_NUM_10    // SPK_BCK
#define AUDIO_SPK_GPIO_DOUT     GPIO_NUM_11    // SPK_DOUT

// ============================================================================
// 显示屏接口 (IIC-4Pin)
// ============================================================================
#define DISPLAY_I2C_GPIO_SCL    GPIO_NUM_1     // SCL 时钟线
#define DISPLAY_I2C_GPIO_SDA    GPIO_NUM_2     // SDA 数据线
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  64
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true


// ============================================================================
// 独立按键接口 
// ============================================================================
#define BOOT_BUTTON_GPIO        GPIO_NUM_0


// ============================================================================
// 电机驱动器 (74HC595D)
// ============================================================================
#define MOTOR_GPIO_SHCP         GPIO_NUM_3     // SMO_SHCP 存储寄存器时钟输入引脚 SCK
#define MOTOR_GPIO_STCP         GPIO_NUM_4     // SMO_STCP 移位寄存器时钟引脚 RCK
#define MOTOR_GPIO_DS           GPIO_NUM_5     // SMO_DS 串行数据输入引脚

// ============================================================================
// LED 指示灯
// ============================================================================
#define LED_GPIO_LED1            GPIO_NUM_48    // LED1
#define LED_GPIO_LED2            GPIO_NUM_7     // LED2

// ============================================================================
// 锂电池检测 (ADC采集)
// ============================================================================
#define BATTERY_GPIO_ADC_USB     GPIO_NUM_6     // ADC采集USB接口电压       ADC1_5
#define BATTERY_GPIO_ADC_BAT     GPIO_NUM_8     // ADC电压采集电池电压      ADC1_7

// ============================================================================
// 摄像头接口 (OV2640)
// ============================================================================
#define CAMERA_GPIO_CAM_D0       GPIO_NUM_42    // CAM_D0
#define CAMERA_GPIO_CAM_D1       GPIO_NUM_45    // CAM_D1
#define CAMERA_GPIO_CAM_D2       GPIO_NUM_46    // CAM_D2
#define CAMERA_GPIO_CAM_D3       GPIO_NUM_41    // CAM_D3
#define CAMERA_GPIO_CAM_D4       GPIO_NUM_16    // CAM_D4
#define CAMERA_GPIO_CAM_D5       GPIO_NUM_18    // CAM_D5
#define CAMERA_GPIO_CAM_D6       GPIO_NUM_17    // CAM_D6
#define CAMERA_GPIO_CAM_D7       GPIO_NUM_40    // CAM_D7

#define CAMERA_GPIO_CAM_XCLK     GPIO_NUM_21    // CAM_XCLK 像素时钟
#define CAMERA_GPIO_CAM_PCLK     GPIO_NUM_15    // CAM_PCLK 像素时钟
#define CAMERA_GPIO_CAM_VSYNC    GPIO_NUM_38    // CAM_VSYNC 垂直同步信号
#define CAMERA_GPIO_CAM_HREF     GPIO_NUM_39    // CAM_HREF 水平参考信号
#define CAMERA_GPIO_CAM_PWDN     GPIO_NUM_47    // CAM_PWDN 电源关闭

#define CAMERA_PIN_RESET    RESET_GPIO_NUM
// add camera
#define CAMERA_GPIO_SIOC GPIO_NUM_1   //和iic 共用一个 scl
#define CAMERA_GPIO_SIOD GPIO_NUM_2   // sda
#define XCLK_FREQ_HZ 20000000


// ============================================================================
// UART接口 (USART0 - IMU数据接入正点原子)
// ============================================================================
#define UART_GPIO_TXD0           GPIO_NUM_43    // TXD0 发送
#define UART_GPIO_RXD0           GPIO_NUM_44    // RXD0 接收

#endif // _IO_CONFIG_H_
