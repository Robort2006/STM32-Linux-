#include "stm32f10x.h"                  // Device header


void Timer1_Init(void)	
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE); //开时钟
	
	TIM_InternalClockConfig(TIM2);		//选时钟源
	
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; //向上计数
	
	TIM_TimeBaseInitStructure.TIM_Period = 5000 - 1;	//ARR 数到ARR就停止，溢出周期 500ms
	TIM_TimeBaseInitStructure.TIM_Prescaler = 7200 - 1;
	//	计数频率 = 72MHz ÷ (PSC+1) = 72M ÷ 7200 = 10kHz（每秒数 1 万下，每下 0.1ms）
	//	溢出周期 = (ARR+1) ÷ 计数频率 = 5000 ÷ 10000Hz = 0.5 秒（500ms，采样节奏）
	
	TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);
	
	TIM_ClearFlag(TIM2, TIM_FLAG_Update); 	//清标志
	TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
	
	
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn; 
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_Init(&NVIC_InitStructure);
	
	TIM_Cmd(TIM2, ENABLE);
}

