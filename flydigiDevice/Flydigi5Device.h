#ifndef _FLYDIGI_DEVICE_H_
#define _FLYDIGI_DEVICE_H_

#include "tusb.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 (仅清零变量，不调 tusb_init)
bool FlydigiDevice_Init(void);

// 任务循环 (1000Hz 发送逻辑)
void FlydigiDevice_Task(void);

// 发送接口
bool flydigi_device_send_report(const uint8_t* report, uint16_t len);

// 挂载状态
bool flydigi_device_mounted(void);

#ifdef __cplusplus
}
#endif

#endif