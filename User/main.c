#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "LED.h"
#include "Serial.h"
#include "Time.h"
#include "ADC.h"
#include "Protocol.h"

volatile uint16_t Adc_Raw = 0;			// ADC 原始值（未滤波，调试用）
volatile uint16_t Adc_Value = 0;		// 8 点滑动平均后的滤波值
volatile uint8_t  Adc_NewFlag = 0;		// 新采样标志：主循环检测到就发一帧
volatile uint8_t  Alarm_Active = 0;		// 超阈报警状态（1=报警中，LED2 闪烁）
volatile uint8_t  Led2_Cmd = 0;			// LED2 命令状态（O=1亮，C=0灭），报警恢复后回到此状态

#define ALARM_THRESHOLD 300				// 报警阈值：3.0V（volt_x100 单位，超过则报警）

// 把当前滤波值换算成电压×100 的整数
static uint16_t Get_VoltX100(void)
{
	return (uint16_t)((uint32_t)Adc_Value * 330 / 4095);
}

/*
 * 报警指示方式说明（LED2 ↔ 蜂鸣器可互换）：
 * 当前用 LED2(PA2) 在超阈时 1Hz 闪烁做报警指示（蜂鸣器太吵，故暂用 LED 代替）。
 * 若要换成蜂鸣器：硬件把蜂鸣器（经三极管驱动）接到任意一个空闲 GPIO；
 * 软件把下面报警相关的 LED2_Turn() / LED2_ON() / LED2_OFF() 换成对应的蜂鸣器翻转/开关即可。
 * 报警只关心"超没超阈"，不关心用什么器件提示，所以阈值、状态机、Stat 位逻辑都不用改。
 */

// 判断是否超阈：用原始值 Adc_Raw（快速响应，不经过8点滤波延迟）
// 上报电压仍用滤波值 Adc_Value（平滑显示）。返回 Stat 状态字节。
static uint8_t Check_Alarm(void)
{
	uint16_t raw_v = (uint16_t)((uint32_t)Adc_Raw * 330 / 4095);	// 原始值换算
	if (raw_v > ALARM_THRESHOLD)
	{
		Alarm_Active = 1;			// 报警中：LED2 交给 TIM2 中断做 1Hz 闪烁
		return STAT_FAULT;			// Stat bit0 = 1，告诉上位机当前是故障状态
	}
	else
	{
		Alarm_Active = 0;
		if (Led2_Cmd) LED2_ON(); else LED2_OFF();	// 恢复正常：回到 O/C 命令设定的状态
		return 0;
	}
}

// 立即采一次样、滤波、判断报警并组帧发送（周期上报和 G 命令共用）
static void Sample_And_Send(void)
{
	uint16_t v;
	uint8_t stat;
	Adc_Raw = ADC_GetValue();
	Adc_Value = Filter_Average(Adc_Raw);
	v = Get_VoltX100();
	stat = Check_Alarm();
	Protocol_SendFrame(v, stat);
}

int main(void)
{
	uint8_t cmd;						// 串口收到的命令字节
	uint16_t v;
	uint8_t stat;

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); //配中断优先级（NVIC）

	LED_Init();
	Serial_Init();
	Timer1_Init();
	ADC1_Init();
	Filter_Init();						// 滤波缓冲区清零

	while (1)
	{
		// 1. 500ms 周期上报一帧
		if (Adc_NewFlag)
		{
			Adc_NewFlag = 0;
			v = Get_VoltX100();
			stat = Check_Alarm();
			Protocol_SendFrame(v, stat);
		}

		// 2. 处理串口命令（非阻塞）
		if (Serial_GetByte(&cmd))
		{
			if (cmd == 'O')			{ Led2_Cmd = 1; if (!Alarm_Active) LED2_ON(); }	// O：命令灯亮
			else if (cmd == 'C')		{ Led2_Cmd = 0; if (!Alarm_Active) LED2_OFF(); }	// C：命令灯灭
			else if (cmd == 'G')		Sample_And_Send();					// G：立即采一次并上报
		}
	}
}

void TIM2_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)
	{
		LED1_Turn();						// LED1 心跳灯（500ms 翻转）
		if (Alarm_Active) LED2_Turn();		// 报警中：LED2 也 500ms 翻转一次 = 1Hz 闪烁
		Adc_Raw = ADC_GetValue();			// 1. 读一次原始采样
		Adc_Value = Filter_Average(Adc_Raw);// 2. 喂进滤波器，得到 8 点平均
		Adc_NewFlag = 1;
		TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
	}
}
