#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 定义数据包结构 (12字节)
typedef struct __attribute__((packed)) {
  uint16_t wButtons;
  uint8_t  bLeftTrigger;
  uint8_t  bRightTrigger;
  int16_t  sThumbLX;
  int16_t  sThumbLY;
  int16_t  sThumbRX;
  int16_t  sThumbRY;
} sram_gamepad_t;

// 共享内存结构
typedef struct {
  volatile uint32_t seq;
  volatile uint32_t timestamp;
  volatile bool     valid;
  uint8_t           _pad[3];
  volatile sram_gamepad_t payload;
} sram_shared_t;

void sram_buffer_init(volatile sram_shared_t* buf);
void sram_buffer_push(volatile sram_shared_t* buf, const sram_gamepad_t* data, bool is_new);
bool sram_buffer_read(volatile sram_shared_t* buf, sram_gamepad_t* out);
uint32_t sram_buffer_get_timestamp(volatile sram_shared_t* buf);

#ifdef __cplusplus
}
#endif