#include "stm32f10x.h"
#include "ADC.h"			// 提供 FILTER_N 宏和本文件函数声明（.c 必须 include 自己的 .h）

void ADC1_Init(void)
{
	// 1. 开启 GPIOA 和 ADC1 时钟（ADC1 挂在 APB2 总线上）
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
	
	// 2. 配置 PA0 为模拟输入（ADC 引脚必须是 AIN 模式，其他模式采样会不准）
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	// 3. ADC 时钟分频：72MHz / 6 = 12MHz（ADC 最高时钟不能超过 14MHz）
	RCC_ADCCLKConfig(RCC_PCLK2_Div6);
	
	// 4. ADC 参数：独立模式、单次转换、右对齐、软件触发
	ADC_InitTypeDef ADC_InitStructure;
	ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;			// 独立模式（不用双 ADC）
	ADC_InitStructure.ADC_ScanConvMode = DISABLE;				// 单通道，不扫描
	ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;			// 单次转换（每次软件触发采一次）
	ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;	// 软件触发
	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;		// 12 位右对齐
	ADC_InitStructure.ADC_NbrOfChannel = 1;						// 规则序列 1 个通道
	ADC_Init(ADC1, &ADC_InitStructure);
	
	// 5. 规则通道 0（PA0 = ADC1_IN0），采样时间 55.5 周期（取长一点，采样更稳）
	ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_55Cycles5);
	
	// 6. 使能 ADC 并校准（F103 必须校准，否则读数整体偏）
	ADC_Cmd(ADC1, ENABLE);
	ADC_ResetCalibration(ADC1);
	while (ADC_GetResetCalibrationStatus(ADC1) == SET); //等复位完成。
	ADC_StartCalibration(ADC1);
	while (ADC_GetCalibrationStatus(ADC1) == SET);
}

uint16_t ADC_GetValue(void)
{
	ADC_SoftwareStartConvCmd(ADC1, ENABLE);			// 软件启动一次转换
	while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET);	// 等转换完成
	return ADC_GetConversionValue(ADC1);			// 读 12 位原始值 0~4095
}

float ADC_GetVoltage(void)
{
	return (float)ADC_GetValue() * 3.3f / 4095.0f;	// 12 位 -> 电压
}

// ===== 8 点滑动平均滤波 =====
static uint16_t filter_buf[FILTER_N];	// 环形缓冲区（static 封装，外部不可见）
static uint8_t  filter_idx = 0;			// 下一个要覆盖的位置
static uint8_t  filter_cnt = 0;			// 已填充个数（前 N 次未满时用它求平均）

void Filter_Init(void)
{
	uint8_t i;
	for (i = 0; i < FILTER_N; i++) filter_buf[i] = 0;
	filter_idx = 0;
	filter_cnt = 0;
}

uint16_t Filter_Average(uint16_t new_val)
{
	uint32_t sum = 0;	// 求和用 32 位，防止以后加大窗口时溢出
	uint8_t i;

	filter_buf[filter_idx] = new_val;			// 新值覆盖最旧值
	filter_idx++;
	if (filter_idx >= FILTER_N) filter_idx = 0;	// 环形回绕
	if (filter_cnt < FILTER_N) filter_cnt++;	// 已填充数，最多到 N

	for (i = 0; i < filter_cnt; i++) sum += filter_buf[i];
	return (uint16_t)(sum / filter_cnt);		// 返回窗口平均
}
