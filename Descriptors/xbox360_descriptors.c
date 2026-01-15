// xbox360_descriptors.c

#include "tusb.h"
#include "xbox360_descriptors.h"

// 让 macOS 认为这是一个标准的 wired Xbox 360 Controller

// =========================================================================
// 1. 设备描述符 (Device Descriptor)
// =========================================================================
tusb_desc_device_t const desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200, // USB 2.0

    // Xbox 360 手柄必须是 Class 0xFF (Vendor Specific)
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    // 【核心】ID 复刻
    .idVendor           = 0x045E, // Microsoft
    .idProduct          = 0x028E, // Xbox 360 Controller
    .bcdDevice          = 0x0114, // 版本号

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

// =========================================================================
// 2. 配置描述符 (Configuration Descriptor)
// =========================================================================

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + 9 + 7 + 7) 
// 9(Interface) + 7(Endpoint IN) + 7(Endpoint OUT) = 23 + 9 = 32 bytes total interface part

uint8_t const desc_configuration[] =
{
    // --- Config Header ---
    // Interface Count: 1, Total Length: CONFIG_TOTAL_LEN
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, 0x00, 500),

    // --- Interface Descriptor ---
    // 对应 uint8_t desc_itf[] = { 9, 4, _itf_num, 0, 2, 0xFF, 0x5D, 0x01, 0 };
    // bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting, bNumEndpoints, bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol, iInterface
    9, TUSB_DESC_INTERFACE, 0, 0, 2, 0xFF, 0x5D, 0x01, 0,

    // --- Endpoint IN Descriptor ---
    // 对应 { 7, 5, _ep_in, 3, 32, 0, 1 };
    // bLength, bDescriptorType, bEndpointAddress, bmAttributes, wMaxPacketSize, bInterval
    7, TUSB_DESC_ENDPOINT, EPNUM_XBOX_IN, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(32), 1,

    // --- Endpoint OUT Descriptor ---
    // 对应 { 7, 5, _ep_out, 3, 32, 0, 8 };
    // 注意：Interval 是 8 (Xbox 标准)
    7, TUSB_DESC_ENDPOINT, EPNUM_XBOX_OUT, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(32), 8
};

// =========================================================================
// 3. 字符串描述符 (String Descriptors)
// =========================================================================
// 数组索引必须对应 Device Descriptor 里的 iManufacturer 等索引
char const* string_desc_arr [] =
{
    (const char[]) { 0x09, 0x04 }, // 0: Supported language is English (0x0409)
    "Microsoft",                   // 1: Manufacturer
    "Controller",                  // 2: Product (macOS 看到这个通常会显示 "Xbox 360 Controller")
    "2025122502",                  // 3: Serials
};

// --- TinyUSB 回调函数 ---

// 返回设备描述符
uint8_t const * tud_descriptor_device_cb(void) {
    return (uint8_t const *) &desc_device;
}

// 返回配置描述符
uint8_t const * tud_descriptor_configuration_cb(uint8_t index) {
    (void) index; // 只有一个配置，忽略 index
    return desc_configuration;
}

// 返回字符串描述符
static uint16_t _desc_str[32];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        // 检查索引是否越界
        if ( !(index < sizeof(string_desc_arr)/sizeof(string_desc_arr[0])) ) return NULL;

        const char* str = string_desc_arr[index];

        // 计算长度 (最大 31 字符)
        chr_count = (uint8_t) strlen(str);
        if (chr_count > 31) chr_count = 31;

        // 转换 ASCII 到 UTF-16
        for(uint8_t i=0; i<chr_count; i++) {
            _desc_str[1+i] = str[i];
        }
    }

    // 第一个字节是长度，第二个字节是类型 (STRING)
    _desc_str[0] = (TUSB_DESC_STRING << 8 ) | (2*chr_count + 2);

    return _desc_str;
}