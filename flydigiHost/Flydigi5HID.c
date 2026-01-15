// Flydigi5HID.c

#include "Flydigi5HID.h"
#include "pico/stdlib.h"
#include "host/usbh.h"
#include "host/usbh_pvt.h"

// =============================================================
// 参数定义
// =============================================================
// 我们要接管 3 个 HID interface：1(vendor),2(mouse),3(keyboard)
#define HID_ITF_VENDOR   1
#define HID_ITF_MOUSE    2
#define HID_ITF_KEYBOARD 3  
#define HID_EP_SIZE     64
#define MAX_HID_IN_EP   3   
#define HID_EP_VENDOR   0x82
#define HID_EP_MOUSE    0x83
#define HID_EP_KEYBOARD 0x84

static tusb_control_request_t _hid_idle_req;
static tusb_control_request_t _hid_report_req;

static tuh_xfer_t _hid_idle_xfer;   // 专门用于 SET_IDLE
static tuh_xfer_t _hid_report_xfer; // 专门用于 SET_REPORT

static tuh_xfer_t _hid_in_xfer[MAX_HID_IN_EP];

static tusb_control_request_t _hid_proto_req;
static tuh_xfer_t _hid_proto_xfer;

static uint8_t flydigi_set_report_payload[10] = {
    0x1c, 0x00, 0x74, 0x07, 0x66,
    0x08, 0xdd, 0xff, 0xff, 0x00
};

typedef struct {
    uint8_t  daddr;

    // [新增] 记录我们是否已接管每个 interface（用于 set_config gate）
    bool itf_claimed[4];        // 下标用 interface number (0..3)
    bool itf_config_done[4];    // set_config_complete 是否已调用

    uint8_t  ep_in[MAX_HID_IN_EP];       // 0x82 / 0x83 / 0x84
    uint16_t ep_in_size[MAX_HID_IN_EP];
    bool     ep_active[MAX_HID_IN_EP];
    bool     ep_in_flight[MAX_HID_IN_EP];

    uint8_t  user_buffer[64];
    volatile uint8_t user_len;
    volatile bool has_new_data;
} flydigi_hid_t;

CFG_TUSB_MEM_ALIGN static uint8_t _usb_rx_buf[MAX_HID_IN_EP][64];

static flydigi_hid_t _hid_dev;

// =============================================================
// 内部逻辑
// =============================================================

// 前向声明
static void kick_hid_in_idx(int idx);

// 传输回调
static void hid_xfer_cb(tuh_xfer_t* xfer)
{
    int idx = (int)(uintptr_t)xfer->user_data;
    if (idx < 0 || idx >= MAX_HID_IN_EP) return;

    _hid_dev.ep_in_flight[idx] = false;

    // === 核心调试打印：只要有 IN 完成，就一定能看到 ===
    printf("[HID] IN done: EP=%02X res=%d len=%u idx=%d\n",
           xfer->ep_addr,
           xfer->result,
           (unsigned)xfer->actual_len,
           idx);

    if (xfer->result == XFER_RESULT_SUCCESS && xfer->actual_len > 0) {
        // 你要的“真正数据”现在先只盯 0x82；0x83/0x84 先确认是否有包即可
        if (_hid_dev.ep_in[idx] == HID_EP_VENDOR) {
            uint8_t len = (xfer->actual_len > 64) ? 64 : xfer->actual_len;
            memcpy(_hid_dev.user_buffer, _usb_rx_buf[idx], len);
            _hid_dev.user_len = len;
            _hid_dev.has_new_data = true;
        } else {
            // 调试建议：先确认 0x83/0x84 是否也有回包
            // printf("[HID] EP %02X len=%u\n", _hid_dev.ep_in[idx], (unsigned)xfer->actual_len);
        }
    }

    // 只续杯本端点
    kick_hid_in_idx(idx);
}

// 发起读取
static void kick_hid_in_idx(int idx)
{
    if (idx < 0 || idx >= MAX_HID_IN_EP) return;
    if (!_hid_dev.ep_active[idx]) return;
    if (_hid_dev.ep_in_flight[idx]) return;
    if (_hid_dev.daddr == 0) return;

    tuh_xfer_t* x = &_hid_in_xfer[idx];

    // 不要 memset(x)
    x->daddr       = _hid_dev.daddr;
    x->ep_addr     = _hid_dev.ep_in[idx];
    x->buflen      = _hid_dev.ep_in_size[idx];
    x->buffer      = _usb_rx_buf[idx];
    x->complete_cb = hid_xfer_cb;
    x->user_data   = (uintptr_t)idx;

    if (tuh_edpt_xfer(x)) {
        _hid_dev.ep_in_flight[idx] = true;
    }
}

static void kick_hid_in_all(void)
{
    for (int i = 0; i < MAX_HID_IN_EP; i++) {
        kick_hid_in_idx(i);
    }
}

static void on_hid_set_protocol_complete(tuh_xfer_t* xfer)
{
  uint8_t itf_num = (uint8_t)xfer->user_data;
  printf("[HID] SET_PROTOCOL complete (res=%d) itf=%u\n", xfer->result, itf_num);

  _hid_dev.itf_config_done[itf_num] = true;
  usbh_driver_set_config_complete(xfer->daddr, itf_num);

  kick_hid_in_all();
}

// [新增] SET_REPORT 完成回调
static void on_hid_set_report_complete(tuh_xfer_t* xfer)
{
    uint8_t itf_num = (uint8_t)xfer->user_data;

    printf("[HID] SET_REPORT complete (res=%d) itf=%u\n", xfer->result, itf_num);

    // 标记 itf1 完成 + 通知 core
    _hid_dev.itf_config_done[itf_num] = true;
    usbh_driver_set_config_complete(xfer->daddr, itf_num);

    // 启动所有已 open 的 interrupt IN
    kick_hid_in_all();
}

static void on_hid_idle_complete(tuh_xfer_t* xfer)
{
  uint8_t itf_num = (uint8_t)xfer->user_data;
  printf("[HID] SET_IDLE complete (res=%d) itf=%u\n", xfer->result, itf_num);

  // itf1：继续 SET_REPORT（你已有逻辑）
  if (itf_num == HID_ITF_VENDOR) {
    printf("[HID] itf=%u sending SET_REPORT\n", itf_num);

    _hid_report_req.bmRequestType_bit.recipient = TUSB_REQ_RCPT_INTERFACE;
    _hid_report_req.bmRequestType_bit.type      = TUSB_REQ_TYPE_CLASS;
    _hid_report_req.bmRequestType_bit.direction = TUSB_DIR_OUT;

    _hid_report_req.bRequest = 0x09;     // SET_REPORT
    _hid_report_req.wValue   = 0x0201;   // 你抓包的 21 09 01 02
    _hid_report_req.wIndex   = itf_num;
    _hid_report_req.wLength  = sizeof(flydigi_set_report_payload);

    memset(&_hid_report_xfer, 0, sizeof(_hid_report_xfer));
    _hid_report_xfer.daddr       = xfer->daddr;
    _hid_report_xfer.ep_addr     = 0;
    _hid_report_xfer.setup       = &_hid_report_req;
    _hid_report_xfer.buffer      = flydigi_set_report_payload;
    _hid_report_xfer.complete_cb = on_hid_set_report_complete;
    _hid_report_xfer.user_data   = (uintptr_t)itf_num;

    if (!tuh_control_xfer(&_hid_report_xfer)) {
      printf("[HID] SET_REPORT queue failed, fallback ready\n");
      _hid_dev.itf_config_done[itf_num] = true;
      usbh_driver_set_config_complete(xfer->daddr, itf_num);
      kick_hid_in_all();
    }
    return;
  }

  // itf2：Boot Mouse -> SET_PROTOCOL(Report=1)
  if (itf_num == HID_ITF_MOUSE) {
    printf("[HID] itf=%u sending SET_PROTOCOL(Report)\n", itf_num);

    _hid_proto_req.bmRequestType_bit.recipient = TUSB_REQ_RCPT_INTERFACE;
    _hid_proto_req.bmRequestType_bit.type      = TUSB_REQ_TYPE_CLASS;
    _hid_proto_req.bmRequestType_bit.direction = TUSB_DIR_OUT;

    _hid_proto_req.bRequest = 0x0B;   // SET_PROTOCOL
    _hid_proto_req.wValue   = 0x0001; // 1 = Report protocol
    _hid_proto_req.wIndex   = itf_num;
    _hid_proto_req.wLength  = 0;

    memset(&_hid_proto_xfer, 0, sizeof(_hid_proto_xfer));
    _hid_proto_xfer.daddr       = xfer->daddr;
    _hid_proto_xfer.ep_addr     = 0;
    _hid_proto_xfer.setup       = &_hid_proto_req;
    _hid_proto_xfer.buffer      = NULL;
    _hid_proto_xfer.complete_cb = on_hid_set_protocol_complete;
    _hid_proto_xfer.user_data   = (uintptr_t)itf_num;

    if (!tuh_control_xfer(&_hid_proto_xfer)) {
      printf("[HID] SET_PROTOCOL queue failed, fallback ready\n");
      _hid_dev.itf_config_done[itf_num] = true;
      usbh_driver_set_config_complete(xfer->daddr, itf_num);
      kick_hid_in_all();
    }
    return;
  }

  // itf3：键盘/其他 HID -> 只要 SET_IDLE 完成就 ready
  _hid_dev.itf_config_done[itf_num] = true;
  usbh_driver_set_config_complete(xfer->daddr, itf_num);
  kick_hid_in_all();
}

static int ep_to_slot(uint8_t addr)
{
    switch (addr) {
        case HID_EP_VENDOR:   return 0;
        case HID_EP_MOUSE:    return 1;
        case HID_EP_KEYBOARD: return 2;
        default: return -1;
    }
}

// =============================================================
// 驱动接口实现
// =============================================================

static bool hid_init(void) {
    memset(&_hid_dev, 0, sizeof(_hid_dev));
    return true;
}

static void hid_close(uint8_t dev_addr) {
    if (_hid_dev.daddr == dev_addr) {
        memset(&_hid_dev, 0, sizeof(_hid_dev));
    }
}

// 打开接口
static bool hid_open(uint8_t rhport, uint8_t dev_addr,
                     tusb_desc_interface_t const *itf_desc,
                     uint16_t max_len)
{
    (void)rhport;

    uint8_t itf = itf_desc->bInterfaceNumber;

    printf("FLYDIGI HID OPEN CALLED (itf=%u class=%u sub=%u proto=%u)\n",
           itf,
           itf_desc->bInterfaceClass,
           itf_desc->bInterfaceSubClass,
           itf_desc->bInterfaceProtocol);

    // 只接管 HID 类接口
    if (itf_desc->bInterfaceClass != TUSB_CLASS_HID) return false;

    // 只接管 1/2/3
    if (itf != HID_ITF_VENDOR && itf != HID_ITF_MOUSE && itf != HID_ITF_KEYBOARD)
        return false;

    // 标记接管了该 interface
    _hid_dev.daddr = dev_addr;
    _hid_dev.itf_claimed[itf] = true;

    // 遍历该 interface 自己的 endpoint
    uint8_t const *p_desc = (uint8_t const *)itf_desc;
    uint8_t const *p_end  = p_desc + max_len;
    p_desc = tu_desc_next(p_desc); // skip interface descriptor

    bool claimed_any = false;

    while (p_desc < p_end)
    {
        if (tu_desc_type(p_desc) == TUSB_DESC_INTERFACE) break;

        if (tu_desc_type(p_desc) == TUSB_DESC_ENDPOINT)
        {
            tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)p_desc;

            // 我们只关心 IN interrupt 端点
            if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN &&
                ep->bmAttributes.xfer == TUSB_XFER_INTERRUPT)
            {
                int slot = ep_to_slot(ep->bEndpointAddress); // 0x82/0x83/0x84 -> 0/1/2
                if (slot >= 0 && !_hid_dev.ep_active[slot])
                {
                    if (tuh_edpt_open(dev_addr, ep))
                    {
                        _hid_dev.ep_in[slot]        = ep->bEndpointAddress;
                        _hid_dev.ep_in_size[slot]   = tu_edpt_packet_size(ep);
                        _hid_dev.ep_active[slot]    = true;
                        _hid_dev.ep_in_flight[slot] = false;

                        printf("[HID] itf %u claimed EP %02X (slot %d, size=%u)\n",
                               itf,
                               _hid_dev.ep_in[slot],
                               slot,
                               (unsigned)_hid_dev.ep_in_size[slot]);

                        claimed_any = true;
                    }
                }
            }
        }

        p_desc = tu_desc_next(p_desc);
    }

    // 对于 mouse/keyboard interface：即使它们没有被我们 slot 收到（极少数情况），也可以返回 true 表示我们接管
    // 但为了稳妥：至少要成功 open 到某个 endpoint 才返回 true
    return true;
}

// 配置完成
// [修改] 配置完成 -> 发送 SET_IDLE
static bool hid_set_config(uint8_t dev_addr, uint8_t itf_num)
{
  if (itf_num != HID_ITF_VENDOR && itf_num != HID_ITF_MOUSE && itf_num != HID_ITF_KEYBOARD)
    return false;
  if (!_hid_dev.itf_claimed[itf_num]) return false;
  if (_hid_dev.itf_config_done[itf_num]) return true;

  printf("[HID] set_config itf=%u -> sending SET_IDLE\n", itf_num);

  _hid_idle_req.bmRequestType_bit.recipient = TUSB_REQ_RCPT_INTERFACE;
  _hid_idle_req.bmRequestType_bit.type      = TUSB_REQ_TYPE_CLASS;
  _hid_idle_req.bmRequestType_bit.direction = TUSB_DIR_OUT;

  _hid_idle_req.bRequest = 0x0A; // SET_IDLE
  _hid_idle_req.wValue   = 0;    // duration=0
  _hid_idle_req.wIndex   = itf_num;
  _hid_idle_req.wLength  = 0;

  memset(&_hid_idle_xfer, 0, sizeof(_hid_idle_xfer));
  _hid_idle_xfer.daddr       = dev_addr;
  _hid_idle_xfer.ep_addr     = 0;
  _hid_idle_xfer.setup       = &_hid_idle_req;
  _hid_idle_xfer.buffer      = NULL;
  _hid_idle_xfer.complete_cb = on_hid_idle_complete;
  _hid_idle_xfer.user_data   = (uintptr_t)itf_num;

  if (!tuh_control_xfer(&_hid_idle_xfer)) {
    printf("[HID] SET_IDLE queue failed, fallback ready\n");
    _hid_dev.itf_config_done[itf_num] = true;
    usbh_driver_set_config_complete(dev_addr, itf_num);
    kick_hid_in_all();
  }

  return true;
}

// 导出驱动
const usbh_class_driver_t flydigi_hid_driver = {
    #if CFG_TUSB_DEBUG >= 2
    .name = "FLY_HID",
    #endif
    .init       = hid_init,
    .open       = hid_open,
    .set_config = hid_set_config,
    .xfer_cb    = NULL, // 使用自定义回调
    .close      = hid_close
};

// =============================================================
// 公开 API
// =============================================================

// 任务函数 (放入主循环)
void Flydigi_HID_Task(void)
{
    // 只对“存在且不在飞”的端点补刀
    for (int i = 0; i < MAX_HID_IN_EP; i++) {
        if (_hid_dev.ep_active[i] && !_hid_dev.ep_in_flight[i]) {
            kick_hid_in_idx(i);
        }
    }
}

// 获取数据
uint8_t flydigi_hid_get_report(uint8_t* buf)
{
    if (_hid_dev.has_new_data) {
        uint8_t len = _hid_dev.user_len;
        memcpy(buf, _hid_dev.user_buffer, len);

        _hid_dev.has_new_data = false;
        return len;
    }
    return 0;
}