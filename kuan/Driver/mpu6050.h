#ifndef __MPU6050_H
#define __MPU6050_H
#include "stm32f4xx.h"

#define MPU6050_ADDR        0x68
#define MPU_WHO_AM_I        0x75
#define MPU_PWR_MGMT_1      0x6B
#define ACCEL_XOUT_H        0x3B

void MPU6050_Init(void);
uint8_t MPU6050_GetID(void);
void MPU6050_ReadAll(int16_t *ax,int16_t *ay,int16_t *az,int16_t *gx,int16_t *gy,int16_t *gz);
void MPU6050_CalibrateGyro(void);

extern int16_t gx_off, gy_off, gz_off;

#endif