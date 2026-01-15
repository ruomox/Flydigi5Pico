// Flydigi5Pico.cpp

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "pio_usb.h"
#include "tusb.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"
// 引入 Host 与 Device 模块
#include "Flydigi5Host.h"
#include "Flydigi5HID.h"
// #include "Flydigi5Device.h"
// 引入 SRAM 模块
#include "SRAM_Buffer.h"
#include "SRAM_Shared.h"
// 引入定义的 Xbox 常量
#include "xbox360_descriptors.h"

// GPIO 针脚定义
#define PIN_USB_DP 12
#define PIN_USB_DM 13

// ============================================================
// 软件暴力复位函数 (强行将 D+/D- 拉低 200ms，模拟物理拔插)
// ============================================================
void usb_force_reset_bus(void)
{
    printf("[MAIN] Forcing USB Bus Reset (Overriding R13)...\n");

    // 1. 初始化 GPIO 为输出模式
    gpio_init(PIN_USB_DP);
    gpio_set_dir(PIN_USB_DP, GPIO_OUT);
    
    gpio_init(PIN_USB_DM);
    gpio_set_dir(PIN_USB_DM, GPIO_OUT);

    // 2. 增强驱动能力 (12mA) 以“战胜”板载上拉电阻
    gpio_set_drive_strength(PIN_USB_DP, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(PIN_USB_DM, GPIO_DRIVE_STRENGTH_12MA);

    // 3. 强制输出低电平 (SE0状态)
    gpio_put(PIN_USB_DP, 0);
    gpio_put(PIN_USB_DM, 0);

    // 4. 保持复位状态 200ms (USB规范要求 >10ms)
    sleep_ms(200);

    // 5. 释放引脚 (恢复为输入，准备交给 PIO 接管)
    gpio_set_dir(PIN_USB_DP, GPIO_IN);
    gpio_set_dir(PIN_USB_DM, GPIO_IN); 
    
    printf("[MAIN] Bus Reset Done. Starting Host Stack...\n");
}

// // ========== [延迟测试] ==========
// // 延迟测试部分 打印频率：10Hz
// #ifndef SRAM_DEBUG_PERIOD_MS
// #define SRAM_DEBUG_PERIOD_MS 100
// #endif
// static inline void print_gp(const char* tag, const sram_gamepad_t* gp, uint32_t latency_us)
// {
//     printf(
//         "[SRAM] %s latency=%lu us | "
//         "BTN=0x%04X LT=%u RT=%u  "
//         "LX=%d LY=%d RX=%d RY=%d\n",
//         tag,
//         (unsigned long)latency_us,
//         gp->wButtons,
//         gp->bLeftTrigger,
//         gp->bRightTrigger,
//         gp->sThumbLX,
//         gp->sThumbLY,
//         gp->sThumbRX,
//         gp->sThumbRY);
// }
// // ========== [延迟测试] ==========

// // ============================================================
// // [Core1] Device
// // ============================================================
// void core1_main(void)
// {
//     // // ========== [延迟测试] ==========
//     // uint32_t last_ts = 0;
//     // // ========== [延迟测试] ==========

//     sleep_ms(100);

//     // 初始化 Device 栈 (Port 0)
//     // 放在这里初始化是最安全的
//     tud_init(0);

//     FlydigiDevice_Init(); // 初始化状态变量

//     while (true) {
//         // 主 Device 模块
//         FlydigiDevice_Task();

//         // // ========== [延迟测试] ==========
//         // // ** 会影响轮询率 仅用作跨核延迟测量 实际使用需关闭 **
//         // sram_gamepad_t gp;
//         // bool is_new = sram_buffer_read(&g_sram_input, &gp);
//         // uint32_t ts  = sram_buffer_get_timestamp(&g_sram_input);
//         // // 端到端内部延迟计算
//         // uint32_t latency_us = time_us_32() - ts;
//         // // 过滤：Host 还没写过任何有效时间戳时，不输出
//         // if (ts == 0)
//         // {
//         //     sleep_ms(SRAM_DEBUG_PERIOD_MS);
//         //     continue;
//         // }
//         // // 可选：只在时间戳变化时输出（更干净）
//         // if (ts != last_ts)
//         // {
//         //     last_ts = ts;
//         //     print_gp(is_new ? "NEW " : "HOLD", &gp, latency_us);
//         // }
//         // sleep_ms(SRAM_DEBUG_PERIOD_MS);
//         // // ========== [延迟测试] ==========

//     }
// }

// ============================================================
// [Core0] Host
// ============================================================
int main(void)
{
    set_sys_clock_khz(120000, true);

    stdio_init_all();

    // Add for Test Hid
    uint8_t hid_buf[64];

    printf("\n=== Flydigi XInput Host Driver Start ===\n");

    printf("sys_clk=%lu Hz\n", clock_get_hz(clk_sys));

    // 执行软件复位
    usb_force_reset_bus();

    // * 接收器可正常挂载的关键 *
    sleep_ms(200);

    // 告诉 TinyUSB 底层使用 GPIO 12/13，且使用标准 DPDM 顺序
    static pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = PIN_USB_DP;
    pio_cfg.pinout = PIO_USB_PINOUT_DPDM; 
    
    // 注入配置 (参数 1 对应 tusb_config.h 中的 RHPORT1)
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

    // 初始化 Host (配置注入，PIO USB 通常映射为 Port 1)
    if (!tuh_init(1)) {
        printf("[MAIN] Error: tuh_init failed!\n");
        while(1);
    }

    sleep_ms(200);

    // // 启动 Core1
    // multicore_launch_core1(core1_main);

    while (true)

    {   
        // 主 Host 进程
        FlydigiHost_Task();

        // ========== [测试Hid] ==========
        Flydigi_HID_Task();

        // 读取 HID 数据
        uint8_t hid_len = flydigi_hid_get_report(hid_buf);
        if (hid_len > 0) {
            printf("[HID] ");
            for(int i=0; i<8; i++) printf("%02X ", hid_buf[i]);
            printf("\n");
        }
        // ========== [测试Hid] ==========
        
        // // ========== [后门] UART 监听重启指令 ==========
        // int c = getchar_timeout_us(0);
        // if (c == 'b' || c == 'B')
        // {
        //     printf("Received command: RESET TO BOOTSEL...\n");
        //     sleep_ms(100);
        //     reset_usb_boot(0, 0);
        // }
        // // ========== [后门] UART 监听重启指令 ==========  
    }
    return 0;
}

// =============================================================
// [核心修复] 统一驱动注册
// =============================================================
extern "C" usbh_class_driver_t const* usbh_app_driver_get_cb(uint8_t* driver_count) {
    // 这里必须定义一个静态数组，包含所有自定义驱动的结构体
    // 注意：不是指针数组，是结构体数组！
    static const usbh_class_driver_t driver_table[] = {
        flydigi_driver_h,      // Interface 0
        flydigi_hid_driver     // Interface 1
    };

    *driver_count = 1; // 驱动数量
    return driver_table; // 返回数组首地址
}


