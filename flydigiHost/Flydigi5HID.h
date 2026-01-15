// Flydigi5HID.h

#ifndef _FLYDIGI_HID_H_
#define _FLYDIGI_HID_H_

#include "tusb.h"
#include "host/usbh_pvt.h"

#ifdef __cplusplus
extern "C" {
#endif

// [新增] HID 任务函数
// 放入 Core 1 的 while(1) 循环中，用于"断链补刀"
void Flydigi_HID_Task(void);

// API: 获取 HID 数据
// 返回数据长度，0 表示无新数据
uint8_t flydigi_hid_get_report(uint8_t* buf);

extern const usbh_class_driver_t flydigi_hid_driver;

#ifdef __cplusplus
}
#endif

#endif