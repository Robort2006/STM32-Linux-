#ifndef _SERIAL_H
#define _SERIAL_H

#include <stdio.h>
#include <stdint.h>

void Serial_SendByte(uint8_t Byte);
void  Serial_Init(void);
void Serial_SendArray(uint8_t *Array, uint16_t Length);
void Serial_SendString(char *String);
void Serial_SendNumber(uint32_t Number, uint8_t Length);
void Serial_Printf(char *format, ...);

uint8_t Serial_GetByte(uint8_t *byte);	// 取一个接收字节；返回1=取到，0=缓冲区空


#endif
