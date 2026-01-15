// SRAM_Buffer.c

#include "SRAM_Buffer.h"
#include <string.h>
#include "pico/time.h"
#include "hardware/sync.h"   // __dmb()
#include "pico/stdlib.h"     // tight_loop_contents()

// ============================================================
// Device -> Host 震动 / LED 邮箱（单帧缓冲）
// ============================================================
volatile bool     g_rumble_valid = false;
volatile uint8_t  g_rumble_data[32] = {0};
volatile uint8_t  g_rumble_len = 0;

void sram_buffer_init(volatile sram_shared_t* buf) {
  buf->seq = 0;
  buf->valid = false;
  buf->timestamp = 0;
  memset((void*)&buf->payload, 0, sizeof(sram_gamepad_t));
  __dmb();
}

void sram_buffer_push(volatile sram_shared_t* buf,
                      const sram_gamepad_t* data,
                      bool is_new) {
  uint32_t seq = buf->seq;

  buf->seq = seq + 1;   // odd: write begin
  __dmb();

  buf->valid = is_new;
  buf->timestamp = time_us_32();

  if (is_new && data) {
    // 对 volatile struct 赋值是合法的；Seqlock 会兜住“读撕裂”
    buf->payload = *data;
  }

  __dmb();
  buf->seq = seq + 2;   // even: write end
}

bool sram_buffer_read(volatile sram_shared_t* buf,
                      sram_gamepad_t* out) {
  uint32_t seq1 = 0;
  uint32_t seq2 = 0;

  do {
    seq1 = buf->seq;
    if (seq1 & 1u) {         // writer in progress
      tight_loop_contents();
      continue;
    }

    __dmb();
    *out = buf->payload;
    __dmb();

    seq2 = buf->seq;
  } while (seq1 != seq2);

  return true;              // true = 本次是“新数据”，false = ZOH 旧数据
}

uint32_t sram_buffer_get_timestamp(volatile sram_shared_t* buf) {
  return buf->timestamp;
}