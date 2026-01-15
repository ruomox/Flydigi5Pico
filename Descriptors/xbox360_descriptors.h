// xbox360_descriptors.h

#ifndef XBOX360_DESCRIPTORS_H_
#define XBOX360_DESCRIPTORS_H_

#ifdef __cplusplus
extern "C" {
#endif

// 定义端点地址
#define EPNUM_XBOX_IN   0x81
#define EPNUM_XBOX_OUT  0x05

// 定义包长 (Xbox 360 标准)
#define CFG_TUD_XINPUT_EPSIZE 32

#ifdef __cplusplus
}
#endif

#endif