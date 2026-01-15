// Flydigi5Device.c

#include "Flydigi5Device.h"
#include "xbox360_descriptors.h" 
#include "SRAM_Shared.h"         

// 必须引用 TinyUSB Device 内部头文件 (注意是 device 目录)
#include "device/usbd.h"
#include "device/usbd_pvt.h"

#include <string.h>

// ============================================================
// 内部状态结构
// ============================================================
typedef struct {
    uint8_t rhport;
    uint8_t itf_num;
    uint8_t ep_in;
    uint8_t ep_out;
    
    volatile bool mounted;
    volatile bool tx_busy;     // IN 发送忙碌标志
    volatile bool out_primed;  // OUT 接收挂载标志 (关键修复)
    
    // OUT 接收缓冲区 (必须对齐)
    CFG_TUSB_MEM_ALIGN uint8_t epout_buf[64];
} xinputd_interface_t;

static xinputd_interface_t _xid_itf;

// 1000Hz 时间轴
static uint32_t g_next_tx_us = 0;
#define TX_INTERVAL_US 1000

// 本地发送缓存 (ZOH)
static sram_gamepad_t g_local_pad;
static uint8_t g_packet[20];

// ============================================================
// 内部辅助：尝试挂载接收 (Prime OUT)
// ============================================================
static void _attempt_prime_out(void) {
    // 如果没连接，或者端点不存在，或者正在忙，或者已经挂载了，就不做
    if (!_xid_itf.mounted || _xid_itf.ep_out == 0) return;
    if (usbd_edpt_busy(_xid_itf.rhport, _xid_itf.ep_out)) return;

    // 尝试挂载接收
    // 如果成功，标记 primed = true
    // 如果失败 (比如 FIFO 满)，标记 false，留给 Task 下一次补刀
    if (usbd_edpt_xfer(_xid_itf.rhport, _xid_itf.ep_out, _xid_itf.epout_buf, 32)) {
        _xid_itf.out_primed = true;
    } else {
        _xid_itf.out_primed = false;
    }
}

// ============================================================
// Class Driver 回调 (底层驱动逻辑)
// ============================================================

// 1. 初始化
static void xid_init(void) {
    memset(&_xid_itf, 0, sizeof(_xid_itf));
}

// 2. 复位
static void xid_reset(uint8_t rhport) {
    (void) rhport;
    memset(&_xid_itf, 0, sizeof(_xid_itf));
}

// 3. 打开接口 (枚举阶段) - [修复] 边界检查逻辑
static uint16_t xid_open(uint8_t rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len) {
    // 1. 检查 Xbox 360 签名
    if (itf_desc->bInterfaceClass != 0xFF || 
        itf_desc->bInterfaceSubClass != 0x5D || 
        itf_desc->bInterfaceProtocol != 0x01) return 0;

    // 2. 基础长度检查
    uint16_t drv_len = sizeof(tusb_desc_interface_t);
    if (max_len < drv_len) return 0;

    // 3. 准备解析
    _xid_itf.rhport = rhport;
    _xid_itf.itf_num = itf_desc->bInterfaceNumber;
    _xid_itf.ep_in = 0;
    _xid_itf.ep_out = 0;

    uint8_t const *p_desc = tu_desc_next(itf_desc);
    int found_eps = 0;

    // 4. 遍历端点 (使用 drv_len 累加，防止越界)
    while (found_eps < 2 && drv_len < max_len) {
        // 获取当前描述符长度
        uint8_t len = tu_desc_len(p_desc);
        if (drv_len + len > max_len) break; // 长度溢出保护

        if (tu_desc_type(p_desc) == TUSB_DESC_ENDPOINT) {
            tusb_desc_endpoint_t const *desc_ep = (tusb_desc_endpoint_t const *)p_desc;
            
            // 打开硬件端点
            if (usbd_edpt_open(rhport, desc_ep)) {
                if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_IN) {
                    _xid_itf.ep_in = desc_ep->bEndpointAddress;
                } else {
                    _xid_itf.ep_out = desc_ep->bEndpointAddress;
                }
                found_eps++;
            }
        }
        
        // 移动指针
        drv_len += len;
        p_desc = tu_desc_next(p_desc);
    }

    _xid_itf.mounted = true;
    _xid_itf.tx_busy = false;
    _xid_itf.out_primed = false;

    // 5. 打开后立即尝试接收
    _attempt_prime_out();

    return drv_len;
}

// 4. 控制传输回调 (处理类请求)
// 简化处理：拦截 SET_REPORT
static bool xid_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_CLASS) {
        if (request->bRequest == 0x09) { // SET_REPORT
            if (stage == CONTROL_STAGE_SETUP) {
                return tud_control_xfer(rhport, request, _xid_itf.epout_buf, request->wLength);
            }
            if (stage == CONTROL_STAGE_ACK) {
                if (request->wLength > 0) {
                    uint16_t len = (request->wLength > 32) ? 32 : request->wLength;
                    memcpy((void*)g_rumble_data, _xid_itf.epout_buf, len);
                    g_rumble_len = (uint8_t)len;
                    g_rumble_valid = true;
                }
                return true;
            }
        }
    }
    return true; 
}

// 5. 数据传输回调 (IN 完成 / OUT 完成)
static bool xid_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
    (void) rhport;

    // --- OUT 数据到达 (震动) ---
    if (ep_addr == _xid_itf.ep_out) {
        // [重要] 标记为未挂载，需要重新挂载
        _xid_itf.out_primed = false;

        if (result == XFER_RESULT_SUCCESS && xferred_bytes > 0) {
            // [桥接] 收到震动数据 -> 存入 SRAM 邮箱
            uint32_t len = (xferred_bytes > 32) ? 32 : xferred_bytes;
            memcpy((void*)g_rumble_data, _xid_itf.epout_buf, len);
            g_rumble_len = (uint8_t)len;
            g_rumble_valid = true;
        }

        // [无限续杯] 尝试立刻接收下一包
        _attempt_prime_out();
    }

    // --- IN 发送完成 ---
    if (ep_addr == _xid_itf.ep_in) {
        _xid_itf.tx_busy = false; // 解锁
    }

    return true;
}

// [修正] 驱动结构体类型必须是 usbd_class_driver_t
static const usbd_class_driver_t _xinputd_driver = {
    #if CFG_TUSB_DEBUG >= 2
    .name = "XINPUT_DEV",
    #endif
    .init             = xid_init,
    .reset            = xid_reset,
    .open             = xid_open,
    .control_xfer_cb  = xid_control_xfer_cb,
    .xfer_cb          = xid_xfer_cb,
    .sof              = NULL
};

// [修正] 注册钩子也必须返回 usbd_class_driver_t
const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &_xinputd_driver;
}

// ============================================================
// Public API Implementation
// ============================================================

bool FlydigiDevice_Init(void) {
    memset(&_xid_itf, 0, sizeof(_xid_itf));
    
    // XInput 包头
    memset(g_packet, 0, 20);
    g_packet[0] = 0x00; 
    g_packet[1] = 0x14;
    
    // 初始化时间轴
    #include "pico/time.h"
    g_next_tx_us = time_us_32();

    // 初始化 Device 栈 (Port 0)
    return tud_init(0);
}

bool flydigi_device_send_report(const uint8_t* report, uint16_t len) {
    if (!_xid_itf.mounted) return false;
    if (_xid_itf.tx_busy) return false; 

    if (usbd_edpt_xfer(_xid_itf.rhport, _xid_itf.ep_in, (uint8_t*)report, len)) {
        _xid_itf.tx_busy = true;
        return true;
    }
    return false;
}

bool flydigi_device_ready(void) {
    return _xid_itf.mounted;
}

// Device 主任务
void FlydigiDevice_Task(void) {
    // 1. 协议栈心跳
    tud_task();

    if (!_xid_itf.mounted) return;

    // 2. [补刀逻辑] 检查 OUT 端点是否掉链子
    // 如果 OUT 端点既不忙(busy=false)，又没有挂载(primed=false)，说明上次续杯失败了
    // 必须在这里重启它，否则震动就断了
    if (_xid_itf.ep_out != 0 && !_xid_itf.out_primed) {
        if (!usbd_edpt_busy(_xid_itf.rhport, _xid_itf.ep_out)) {
            _attempt_prime_out();
        }
    }

    // 3. 1000Hz 发送逻辑 (ZOH)
    uint32_t now = time_us_32();
    int32_t diff = (int32_t)(now - g_next_tx_us);
    if (diff < 0) return;

    // 推进时间
    g_next_tx_us += TX_INTERVAL_US;
    if (diff > 5000) g_next_tx_us = now + TX_INTERVAL_US;

    // 发送检查
    if (_xid_itf.tx_busy) return;
    
    // 准备数据
    sram_buffer_read((sram_shared_t*)&g_sram_input, &g_local_pad);
    memcpy(&g_packet[2], &g_local_pad, 12);

    // 发送
    if (usbd_edpt_xfer(_xid_itf.rhport, _xid_itf.ep_in, g_packet, 20)) {
        _xid_itf.tx_busy = true;
    }
}
