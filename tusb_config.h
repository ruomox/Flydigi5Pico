#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#undef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG            2

// --- DEVICE 配置 (模拟 Xbox 360) ---
#define CFG_TUD_ENABLED           1
// 原生 USB 口 -> 接电脑 (Device)
#define CFG_TUSB_RHPORT0_MODE     OPT_MODE_DEVICE
// 端点缓冲大小 (Xbox 数据包通常较小，64字节足够)
#define CFG_TUD_ENDPOINT0_SIZE    64

// 其他 DEVICE 类关闭
#define CFG_TUD_CDC               0
#define CFG_TUD_MSC               0
#define CFG_TUD_HID               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0

// --- HOST 配置 (读取接收器) ---
#define CFG_TUH_ENABLED           1
// 告诉 TinyUSB 我们使用 PIO-USB 作为 Host 控制器
#define CFG_TUH_RPI_PIO_USB       1
// 逻辑端口 1 为 Host
#define CFG_TUH_RHPORT1_MODE      OPT_MODE_HOST
// 最大设备数 (接收器算1个)
#define CFG_TUH_DEVICE_MAX        3
// 枚举缓冲大小 (256字节足够读取描述符)
#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUSB_HOST             1

// 自定义 Flydigi Host
#define CFG_TUH_FLYDIGI           1

// 其他 Host 类关闭
#define CFG_TUH_CDC               0
#define CFG_TUH_MSC               0
#define CFG_TUH_HUB               0
#define CFG_TUH_VENDOR            0
// 为了自定义 HID 接管故关闭
#define CFG_TUH_HID               0

// 原生 HID 数据端点缓冲
#define CFG_TUH_HID_EPIN_BUFSIZE  64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64

#ifdef __cplusplus
}
#endif

#endif