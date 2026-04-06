#ifndef __BH1750_H
#define __BH1750_H

#include "stm32f10x.h"

void BH1750_Init(void);
uint8_t BH1750_ReadLux(uint16_t *Lux);

#endif
