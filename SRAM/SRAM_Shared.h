#pragma once
#include "SRAM_Buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

// =======================================================
// 上行通道：Host -> Device (输入数据)
// =======================================================

// 由 Host 驱动 .c 定义（不要在这里定义）
extern volatile sram_shared_t g_sram_input;


// =======================================================
// 下行通道：Device -> Host (震动 / LED / 原始指令)
// =======================================================

// 是否有新的下行指令
extern volatile bool     g_rumble_valid;

// 原始下行指令数据（盲转发，不解析）
extern volatile uint8_t  g_rumble_data[32];

// 实际指令长度（Xbox 常见为 8）
extern volatile uint8_t  g_rumble_len;

#ifdef __cplusplus
}
#endif