// SPDX-License-Identifier: MIT
//
// Based on code from usb64
// Copyright (c) 2020 Ryan Wendland
//
// Modified and refactored by Mox ZaZa, 2025
// Flydigi5Host.c

#include "tusb_option.h"

#if (TUSB_OPT_HOST_ENABLED && CFG_TUH_FLYDIGI)

#include "host/usbh.h"
#include "Flydigi5Host.h"
#include "SRAM_Buffer.h"
#include "SRAM_Shared.h"

// [SRAM] 导出全局符号
volatile sram_shared_t g_sram_input;

// Host 状态机
typedef enum {
    HOST_WAIT_POWERUP,
    HOST_SCANNING,
    HOST_ATTACHED
} flydigi_host_state_t;

static flydigi_host_state_t g_host_state = HOST_WAIT_POWERUP;
static absolute_time_t g_next_scan_time;

// Flydigi 5 commands
static const uint8_t flydigi_rumble[] = {0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

typedef struct
{
    uint8_t inst_count;
    xinputh_interface_t instances[CFG_TUH_FLYDIGI];
} xinputh_device_t;

static xinputh_device_t _xinputh_dev[CFG_TUH_DEVICE_MAX];

TU_ATTR_ALWAYS_INLINE static inline xinputh_device_t *get_dev(uint8_t dev_addr)
{
    return &_xinputh_dev[dev_addr - 1];
}

TU_ATTR_ALWAYS_INLINE static inline xinputh_interface_t *get_instance(uint8_t dev_addr, uint8_t instance)
{
    return &_xinputh_dev[dev_addr - 1].instances[instance];
}

static uint8_t get_instance_id_by_epaddr(uint8_t dev_addr, uint8_t ep_addr)
{
    for (uint8_t inst = 0; inst < CFG_TUH_FLYDIGI; inst++)
    {
        xinputh_interface_t *hid = get_instance(dev_addr, inst);

        if ((ep_addr == hid->ep_in) || (ep_addr == hid->ep_out))
            return inst;
    }
    return 0xff;
}

static uint8_t get_instance_id_by_itfnum(uint8_t dev_addr, uint8_t itf)
{
    for (uint8_t inst = 0; inst < CFG_TUH_FLYDIGI; inst++)
    {
        xinputh_interface_t *hid = get_instance(dev_addr, inst);

        if ((hid->itf_num == itf) && (hid->ep_in || hid->ep_out))
            return inst;
    }
    return 0xff;
}

static void wait_for_tx_complete(uint8_t dev_addr, uint8_t ep_out)
{
    while (usbh_edpt_busy(dev_addr, ep_out))
        tuh_task();
}

bool tuh_xinput_receive_report(uint8_t dev_addr, uint8_t instance)
{
    xinputh_interface_t *xid_itf = get_instance(dev_addr, instance);
    TU_VERIFY(usbh_edpt_claim(dev_addr, xid_itf->ep_in));

    if ( !usbh_edpt_xfer(dev_addr, xid_itf->ep_in, xid_itf->epin_buf, xid_itf->epin_size) )
    {
        usbh_edpt_release(dev_addr, xid_itf->ep_in);
        return false;
    }
    return true;
}

bool tuh_xinput_send_report(uint8_t dev_addr, uint8_t instance, const uint8_t *txbuf, uint16_t len)
{
    xinputh_interface_t *xid_itf = get_instance(dev_addr, instance);

    TU_ASSERT(len <= xid_itf->epout_size);
    // 1. Claim OUT endpoint
    if (!usbh_edpt_claim(dev_addr, xid_itf->ep_out))
        return false;

    // 2. Copy data
    memcpy(xid_itf->epout_buf, txbuf, len);
    
    // 3. Start transfer
    if (!usbh_edpt_xfer(dev_addr, xid_itf->ep_out, xid_itf->epout_buf, len))
    {
        // ★ 关键：失败必须 release
        usbh_edpt_release(dev_addr, xid_itf->ep_out);
        return false;
    }
    // 成功：release 交给 xfer_cb
    return true;
}

bool tuh_xinput_set_rumble(uint8_t dev_addr, uint8_t instance, uint8_t lValue, uint8_t rValue, bool block)
{
    xinputh_interface_t *xid_itf = get_instance(dev_addr, instance);
    uint8_t txbuf[8];
    uint16_t len = sizeof(flydigi_rumble);

    memcpy(txbuf, flydigi_rumble, len);

    // 填入马达强度
    // (注意：数组下标从0开始，所以是 txbuf[4] 和 txbuf[6])
    txbuf[3] = lValue; 
    txbuf[4] = rValue;

    bool ret = tuh_xinput_send_report(dev_addr, instance, txbuf, len);
    
    if (block && ret)
    {
        wait_for_tx_complete(dev_addr, xid_itf->ep_out);
    }
    return true;
}

//--------------------------------------------------------------------+
// USBH API
//--------------------------------------------------------------------+
bool xinputh_init(void)
{
    tu_memclr(_xinputh_dev, sizeof(_xinputh_dev));
    sram_buffer_init(&g_sram_input);
    return true;
}

bool xinputh_open(uint8_t rhport, uint8_t dev_addr, tusb_desc_interface_t const *desc_itf, uint16_t max_len)
{
    (void) rhport;
    TU_VERIFY(dev_addr <= CFG_TUH_DEVICE_MAX);

    xinput_type_t type = XINPUT_UNKNOWN;
    if (desc_itf->bNumEndpoints >= 2)
    {
        const uint8_t cls = desc_itf->bInterfaceClass;
        (void) cls;
        const uint8_t sub = desc_itf->bInterfaceSubClass;
        const uint8_t pro = desc_itf->bInterfaceProtocol;
        // 目标：匹配“行为上是 XInput”的接口，而不是死盯 VID/PID。
        //
        // 1) Xbox 360 Wireless Receiver 常见签名 (微软原版)
        //    Class 通常为 0xFF (Vendor)，SubClass=0x5D, Protocol=0x81
        //
        // 2) 大量第三方/国产接收器会用 Protocol=0x01（看起来更像“有线”行为）
        //
        // 你可以先只收敛到 0x5D 子类，再逐步收紧。
        if (sub == 0x5D && (pro == 0x81 || pro == 0x01))
        {
            type = XINPUT_STANDARD;
        }
        // 如果你后面发现飞智的 class 不是 0xFF，而是 0x58/其他，
        // 可以再补一条更宽松或更精确的判断。
        // 例如：if (cls == 0xFF && sub == 0x5D && (pro == 0x81 || pro == 0x01)) ...
    }

    // 不是 XInput，立刻拒绝
    if (type == XINPUT_UNKNOWN)
    {
        return false;
    }

    TU_LOG2("XINPUT opening Interface %u (addr = %u)\r\n", desc_itf->bInterfaceNumber, dev_addr);

    xinputh_device_t *xinput_dev = get_dev(dev_addr);
    TU_ASSERT(xinput_dev->inst_count < CFG_TUH_FLYDIGI, 0);

    xinputh_interface_t *xid_itf = get_instance(dev_addr, xinput_dev->inst_count);
    xid_itf->itf_num = desc_itf->bInterfaceNumber;
    xid_itf->type = type;

    //Parse descriptor for all endpoints and open them
    uint8_t const *p_desc = (uint8_t const *)desc_itf;
    int endpoint = 0;
    int pos = 0;
    while (endpoint < desc_itf->bNumEndpoints && pos < max_len)
    {
        if (tu_desc_type(p_desc) != TUSB_DESC_ENDPOINT)
        {
            pos += tu_desc_len(p_desc);
            p_desc = tu_desc_next(p_desc);
            continue;
        }
        tusb_desc_endpoint_t const *desc_ep = (tusb_desc_endpoint_t const *)p_desc;
        TU_ASSERT(TUSB_DESC_ENDPOINT == desc_ep->bDescriptorType);
        TU_ASSERT(tuh_edpt_open(dev_addr, desc_ep));
        if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_OUT)
        {
            xid_itf->ep_out = desc_ep->bEndpointAddress;
            xid_itf->epout_size = tu_edpt_packet_size(desc_ep);
        }
        else
        {
            xid_itf->ep_in = desc_ep->bEndpointAddress;
            xid_itf->epin_size = tu_edpt_packet_size(desc_ep);
        }
        endpoint++;
        pos += tu_desc_len(p_desc);
        p_desc = tu_desc_next(p_desc);
    }

    xinput_dev->inst_count++;
    return true;
}

bool xinputh_set_config(uint8_t dev_addr, uint8_t itf_num)
{
    uint8_t instance = get_instance_id_by_itfnum(dev_addr, itf_num);
    xinputh_interface_t* itf = get_instance(dev_addr, instance);

    itf->connected = true;
    itf->in_flight = false;
    itf->next_in_us = time_us_32();
    itf->sram_armed = false;   // ★ 新增：未收到有效数据前，不允许 SRAM 运行

    if (tuh_xinput_mount_cb) tuh_xinput_mount_cb(dev_addr, instance, itf);

    // 很关键：告诉 usbh 这个 class driver 已完成配置
    usbh_driver_set_config_complete(dev_addr, itf->itf_num);
    return true;
}

bool xinputh_xfer_cb(uint8_t dev_addr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{   
    // ★ NEW 1/4：先拿到方向和 instance（release 需要）
    uint8_t const dir = tu_edpt_dir(ep_addr);
    uint8_t const instance = get_instance_id_by_epaddr(dev_addr, ep_addr);
    xinputh_interface_t *xid_itf = get_instance(dev_addr, instance);

    // ★ NEW 2/4：无论成功失败，都必须 release 这个 EP
    usbh_edpt_release(dev_addr, ep_addr);
    
    // 2. 如果这是 IN 端点，本次 IN 生命周期结束
    if (dir == TUSB_DIR_IN)
    {
        xid_itf->in_flight = false;
    
    }

    if (dir == TUSB_DIR_IN)
    {
        // 无论成功失败，这一毫秒 Host 都“采样”了一次
        if (result != XFER_RESULT_SUCCESS)
        {
            // 不更新 payload，记录“本 tick 没新数据”
            sram_buffer_push(&g_sram_input, NULL, false);
            TU_LOG1("Error: %d\n", result);
            return false; // 保持你原本的返回策略
        }
    }
    else
    {
        if (result != XFER_RESULT_SUCCESS)
        {
            TU_LOG1("Error: %d\n", result);
            return false;
        }
    }

    if (dir == TUSB_DIR_IN)
    {
        uint8_t *rdata = xid_itf->epin_buf;
        xinput_gamepad_t *pad = &xid_itf->pad;
        TU_LOG2("Get Report callback (%u, %u, %lu bytes)\r\n",dev_addr, instance, (unsigned long)xferred_bytes);
        TU_LOG2_MEM(xid_itf->epin_buf, xferred_bytes, 2);
        // XInput standard input report
        if (xferred_bytes >= 14 && rdata[1] == 0x14)
        {   
            tu_memclr(pad, sizeof(xinput_gamepad_t));

            uint16_t wButtons = rdata[3] << 8 | rdata[2];

            //Map digital buttons
            if (wButtons & (1 << 0)) pad->wButtons |= XINPUT_GAMEPAD_DPAD_UP;
            if (wButtons & (1 << 1)) pad->wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
            if (wButtons & (1 << 2)) pad->wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
            if (wButtons & (1 << 3)) pad->wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
            if (wButtons & (1 << 4)) pad->wButtons |= XINPUT_GAMEPAD_START;
            if (wButtons & (1 << 5)) pad->wButtons |= XINPUT_GAMEPAD_BACK;
            if (wButtons & (1 << 6)) pad->wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
            if (wButtons & (1 << 7)) pad->wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
            if (wButtons & (1 << 8)) pad->wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
            if (wButtons & (1 << 9)) pad->wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
            if (wButtons & (1 << 10)) pad->wButtons |= XINPUT_GAMEPAD_GUIDE;
            if (wButtons & (1 << 12)) pad->wButtons |= XINPUT_GAMEPAD_A;
            if (wButtons & (1 << 13)) pad->wButtons |= XINPUT_GAMEPAD_B;
            if (wButtons & (1 << 14)) pad->wButtons |= XINPUT_GAMEPAD_X;
            if (wButtons & (1 << 15)) pad->wButtons |= XINPUT_GAMEPAD_Y;

            //Map the left and right triggers
            pad->bLeftTrigger = rdata[4];
            pad->bRightTrigger = rdata[5];

            // Map analog sticks (STRICT signed handling)
            uint16_t u;
            // LX
            u = ((uint16_t)rdata[7] << 8) | (uint16_t)rdata[6];
            pad->sThumbLX = (int16_t)u;
            // LY
            u = ((uint16_t)rdata[9] << 8) | (uint16_t)rdata[8];
            pad->sThumbLY = (int16_t)u;
            // RX
            u = ((uint16_t)rdata[11] << 8) | (uint16_t)rdata[10];
            pad->sThumbRX = (int16_t)u;
            // RY
            u = ((uint16_t)rdata[13] << 8) | (uint16_t)rdata[12];
            pad->sThumbRY = (int16_t)u;

            // 解析已经写入 xid_itf->pad
            // 把 Host 解析后的结构体写入 SRAM（结构化传递）
            sram_gamepad_t gp = {
                .wButtons      = xid_itf->pad.wButtons,
                .bLeftTrigger  = xid_itf->pad.bLeftTrigger,
                .bRightTrigger = xid_itf->pad.bRightTrigger,
                .sThumbLX      = xid_itf->pad.sThumbLX,
                .sThumbLY      = xid_itf->pad.sThumbLY,
                .sThumbRX      = xid_itf->pad.sThumbRX,
                .sThumbRY      = xid_itf->pad.sThumbRY
            };
            sram_buffer_push(&g_sram_input, &gp, true);
        }
        else
        {
            // 成功收到了包，但不是你要的输入包：按“没新数据”处理
            sram_buffer_push(&g_sram_input, NULL, false);
        }

        tuh_xinput_report_received_cb(dev_addr, instance, (const uint8_t *)xid_itf, sizeof(xinputh_interface_t));
    }
    else
    {
        if (tuh_xinput_report_sent_cb)
        {
            tuh_xinput_report_sent_cb(dev_addr, instance, xid_itf->epout_buf, xferred_bytes);
        }
    }

    return true;
}

void xinputh_close(uint8_t dev_addr)
{
    TU_VERIFY(dev_addr <= CFG_TUH_DEVICE_MAX, );
    xinputh_device_t *xinput_dev = get_dev(dev_addr);

    for (uint8_t inst = 0; inst < xinput_dev->inst_count; inst++)
    {
        if (tuh_xinput_umount_cb)
        {
            tuh_xinput_umount_cb(dev_addr, inst);
        }
    }
    tu_memclr(xinput_dev, sizeof(xinputh_device_t));
}

const usbh_class_driver_t flydigi_driver_h =
    {
      
    #if CFG_TUSB_DEBUG >= 2
        .name = "XINPUT",
    #endif
      .init       = xinputh_init,
      .open       = xinputh_open,
      .set_config = xinputh_set_config,
      .xfer_cb    = xinputh_xfer_cb,
      .close      = xinputh_close
    };

// ============================================================
// FlydigiHost_Task 主入口函数
// ============================================================
void FlydigiHost_Task(void)
{
    switch (g_host_state)
    {
        case HOST_WAIT_POWERUP:
            // 上电后给无线接收器一点准备时间
            g_next_scan_time = make_timeout_time_ms(1500);
            g_host_state = HOST_SCANNING;
            break;

        case HOST_SCANNING:
            if (absolute_time_diff_us(get_absolute_time(), g_next_scan_time) <= 0)
            {
                // 到时间了，允许 TinyUSB Host 跑一次
                tuh_task();

                // 每 200ms 允许重试一次
                g_next_scan_time = make_timeout_time_ms(10);
            }
            break;

        case HOST_ATTACHED:
        {
            // 必须持续运行协议栈
            tuh_task();

            uint32_t now = time_us_32();

            // 遍历所有可能的设备槽位
            for (uint8_t dev = 1; dev <= CFG_TUH_DEVICE_MAX; dev++)
            {
                // [安全修正] 检查设备指针有效性
                xinputh_device_t* d = get_dev(dev);
                if (d == NULL || d->inst_count == 0) continue;

                // 遍历接口 (通常飞智只有一个 XInput 接口实例)
                for (uint8_t inst = 0; inst < d->inst_count; inst++)
                {
                    xinputh_interface_t* itf = get_instance(dev, inst);

                    // [安全修正] 确保接口已连接
                    if (!itf->connected) continue;

                    // -------------------------------------------------
                    // [原有逻辑] 1. IN 轮询 (1000Hz 读取手柄输入)
                    // -------------------------------------------------
                    int32_t diff = (int32_t)(now - itf->next_in_us);
                    if (diff >= 0)
                    {
                        // ===== 1. 触发一次轮询（如果允许）=====
                        if (!itf->in_flight)
                        {
                            if (tuh_xinput_receive_report(dev, inst))
                            {
                                itf->in_flight = true;
                            }
                        }

                        // ===== 2. 时间轴修正（关键）=====
                        if (diff > 2000)
                        {
                            // 落后超过 2ms，认为系统曾卡顿
                            // 不追历史，直接回到“当前 + 1ms”
                            itf->next_in_us = now + 1000;
                        }
                        else
                        {
                            // 推进时间轴，严格 1000Hz，不用 now + 1000 
                            itf->next_in_us += 1000;
                        }
                    }
                    // -------------------------------------------------
                    // [新增逻辑] 2. OUT 转发 (处理 Mac 发来的震动)
                    // -------------------------------------------------
                    // 检查全局邮箱是否有新指令
                    if (g_rumble_valid)
                    {
                        // 尝试发送震动包
                        // (强转 const uint8_t* 是为了匹配函数签名，g_rumble_data 是 volatile)
                        if (tuh_xinput_send_report(dev, inst, (const uint8_t*)g_rumble_data, g_rumble_len))
                        {
                            // ★ 只有发送成功(返回true)才清除标志
                            // 如果失败(比如端点忙)，标志保留，下一轮循环自动重试
                            g_rumble_valid = false;
                        }
                    }
                }
            }
        }
        break;
    }
}


// ============================================================
// XInput 报告回调（应用层使用驱动的入口）
// ============================================================
void tuh_xinput_report_received_cb(uint8_t dev_addr,
                                   uint8_t instance,
                                   uint8_t const* report,
                                   uint16_t len)
{
    (void) dev_addr;
    (void) instance;
    (void) report;
    (void) len;
    // 已弃用：数据路径已迁移至 SRAM + Core1
}

void tuh_hid_report_received_cb(
    uint8_t dev_addr,
    uint8_t instance,
    uint8_t const* report,
    uint16_t len)
{
    // 你现在不用 HID，留空即可
    (void) dev_addr;
    (void) instance;
    (void) report;
    (void) len;
}
// Host 回调
void tuh_mount_cb(uint8_t dev_addr)
{
    printf("[HOST] Device %d mounted\n", dev_addr);
    g_host_state = HOST_ATTACHED;
}

usbh_class_driver_t const* usbh_app_driver_get_cb(uint8_t* driver_count)
{
*driver_count = 1;
return &flydigi_driver_h;
}

#endif /* TUSB_OPT_HOST_ENABLED && CFG_TUH_FLYDIGI */