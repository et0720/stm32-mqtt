#ifndef __STH30_H
#define __STH30_H

#include "stm32f10x.h"

typedef struct
{
	int16_t Temperature;
	uint16_t Humidity;
} STH30_DataTypeDef;

void STH30_Init(void);
uint8_t STH30_ReadData(STH30_DataTypeDef *Data);

#endif
