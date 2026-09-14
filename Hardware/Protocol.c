#include "stm32f10x.h"
#include "Protocol.h"
#include "Serial.h"

void Protocol_SendFrame(uint16_t volt_x100, uint8_t stat)
{
	uint8_t frame[FRAME_LEN];
	uint8_t sum = 0;
	uint8_t i;

	frame[0] = FRAME_HEAD;                    // 帧头 AA
	frame[1] = FRAME_CH;                      // 通道号 01
	frame[2] = (uint8_t)(volt_x100 >> 8);     // 电压高字节 VolH // >> 右移运算符
	frame[3] = (uint8_t)(volt_x100 & 0xFF);   // 电压低字节 VolL // 与运算，只保留低八位
	frame[4] = stat;                          // 状态字节 Stat

	// 校验和：前 5 字节累加，取低 8 位（uint8_t 自动溢出即取模 256）
	for (i = 0; i < FRAME_LEN - 1; i++) sum += frame[i];
	frame[5] = sum;                           // SUM

	Serial_SendArray(frame, FRAME_LEN);       // 整帧一次性发出
}
