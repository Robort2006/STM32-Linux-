#ifndef _ADC_H
#define _ADC_H


void ADC1_Init(void);
uint16_t ADC_GetValue(void); 	// 软件启动一次转换，返回 12 位原始值 0~4095（阻塞等待完成）
float ADC_GetVoltage(void);		// 返回换算后的电压，单位 V（0~3.3V）

// ===== 8 点滑动平均滤波 =====
#define FILTER_N 8						// 滤波窗口长度
void Filter_Init(void);					// 滤波器清零（上电时调一次）
uint16_t Filter_Average(uint16_t new_val);	// 喂入一次新采样，返回当前窗口平均值

#endif
