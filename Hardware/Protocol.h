#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#include <stdint.h>

// ===== 私有帧结构：AA 01 VolH VolL Stat SUM，共 6 字节 =====
#define FRAME_HEAD   0xAA	// 帧头（帧同步标志）
#define FRAME_CH     0x01	// 通道号
#define FRAME_LEN    6		// 整帧长度

// Stat 状态位定义
#define STAT_FAULT   0x01	// bit0：超阈报警（2.4 步启用，本步先填 0）

// 组一帧并通过串口发出：volt_x100=电压×100 的整数，stat=状态字节
void Protocol_SendFrame(uint16_t volt_x100, uint8_t stat);

#endif
