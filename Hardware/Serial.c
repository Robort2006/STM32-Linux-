#include "stm32f10x.h"                  // Device header
#include "Serial.h"
#include <stdio.h>
#include <stdarg.h>

// ===== 接收环形缓冲区 =====
#define RX_BUF_SIZE 64					// 缓冲区大小（必须是 2 的幂次更高效，这里用取模通用写法）
static uint8_t RxBuffer[RX_BUF_SIZE];
static volatile uint8_t RxHead = 0;		// 写入位置（中断=生产者推进）
static volatile uint8_t RxTail = 0;		// 读取位置（主循环=消费者推进）

void  Serial_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

	GPIO_InitTypeDef GPIO_InitStructure;
	// PA9 = USART1_TX：复用推挽输出
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	// PA10 = USART1_RX：浮空输入
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	USART_InitTypeDef USART_InitStructure;
	USART_InitStructure.USART_BaudRate = 115200;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;	// 收发都开（原来只有 Tx）
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_Init(USART1, &USART_InitStructure);

	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);	// 收到一个字节就触发 RXNE 中断

	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;	// 比 TIM2(抢占2) 高，保证接收不丢字节
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_Init(&NVIC_InitStructure);

	USART_Cmd(USART1, ENABLE);
}

// USART1 接收中断服务函数：每收到 1 字节进一次，只做"存入缓冲区"这一件快事
void USART1_IRQHandler(void)
{
	if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
	{
		uint8_t ch = (uint8_t)USART_ReceiveData(USART1);	// 读 DR，同时清 RXNE 标志
		uint8_t next = (RxHead + 1) % RX_BUF_SIZE;
		if (next != RxTail)			// 缓冲区没满才写入；满了丢弃，防止覆盖还没读走的数据
		{
			RxBuffer[RxHead] = ch;
			RxHead = next;
		}
	}
}

// 主循环调用：非阻塞取一个字节。返回 1=取到（结果放 *byte），0=缓冲区空
uint8_t Serial_GetByte(uint8_t *byte)
{
	if (RxHead == RxTail) return 0;		// 头尾相等 = 空
	*byte = RxBuffer[RxTail];
	RxTail = (RxTail + 1) % RX_BUF_SIZE;
	return 1;
}

void Serial_SendByte(uint8_t Byte)
{
	USART_SendData(USART1, Byte);
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
}

void Serial_SendArray(uint8_t *Array, uint16_t Length)
{
	uint16_t i;
	for(i =0; i<Length; i++)
	{
		Serial_SendByte(Array[i]);
	}
}

void Serial_SendString(char *String)
{
	uint8_t i;
	for(i = 0; String[i] != '\0'; i++)
	{
		Serial_SendByte(String[i]);
	}
}

uint32_t Serial_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result =1;
	while(Y--){
		Result *= X;
	}
	return Result;
}


void Serial_SendNumber(uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for(i=0; i< Length; i++){
		Serial_SendByte(Number / Serial_Pow(10, Length - i - 1) % 10 + '0');
	}
}

int fputc(int ch, FILE *f)
{
	Serial_SendByte(ch);
	return ch;
}

void Serial_Printf(char *format, ...)
{
	char String[100];
	va_list arg;
	va_start(arg, format);
	vsprintf(String, format, arg);
	va_end(arg);
	Serial_SendString(String);

}
