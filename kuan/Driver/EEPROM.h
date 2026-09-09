#ifndef __EEPROM_H
#define __EEPROM_H

#include "stm32f4xx.h"
#include "Delay.h"
#include <stdio.h>
#include "IIC.h"


void EEPROM_Config(void);
uint8_t EEPROM_WriteByte(uint8_t add,uint8_t data);
uint8_t EEPROM_PageWrite(uint8_t pageAdd, uint8_t datasize, uint8_t *dataptr);
uint8_t EEPROM_ReadCurrAdd(void);
uint8_t EEPROM_ReadByte(uint8_t Add);
void EEPROM_Test();
#endif
