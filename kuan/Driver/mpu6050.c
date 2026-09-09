#include "mpu6050.h"
#include "mpuiic.h"

//Ð´MPU¼Ä´æÆ÷
static void MPU6050_WriteReg(uint8_t dev_addr,uint8_t reg,uint8_t data)
{
    MPU_IIC_Start();
    MPU_IIC_WriteByte(dev_addr << 1);
    MPU_IIC_WriteByte(reg);
    MPU_IIC_WriteByte(data);
    MPU_IIC_Stop();
}

//¶ÁMPU¼Ä´æÆ÷
static uint8_t MPU6050_ReadReg(uint8_t dev_addr,uint8_t reg)
{
    uint8_t val;
    MPU_IIC_Start();
    MPU_IIC_WriteByte(dev_addr << 1);
    MPU_IIC_WriteByte(reg);

    MPU_IIC_Start();
    MPU_IIC_WriteByte((dev_addr << 1)|0x01);
    val = MPU_IIC_ReadByte(1);
    MPU_IIC_Stop();
    return val;
}


void MPU6050_Init(void)
{
	MPU_IIC_GPIO_Init();
	//»½ÐÑ£¬ÍË³öÐÝÃß
	MPU6050_WriteReg(MPU6050_ADDR, MPU_PWR_MGMT_1, 0x01);
	delay_ms(100);

	//ACCEL_CONFIG £º¡À2g
	MPU6050_WriteReg(MPU6050_ADDR,0x1C,0x00);
	//GYRO_CONFIG£º¡À250¡ã/s
	MPU6050_WriteReg(MPU6050_ADDR,0x1B,0x00);
}

uint8_t MPU6050_GetID(void)
{
	return MPU6050_ReadReg(MPU6050_ADDR, MPU_WHO_AM_I);
}

//¶ÁÈ¡6ÖáÔ­Ê¼Êý¾Ý
void MPU6050_ReadAll(int16_t *ax,int16_t *ay,int16_t *az,
                     int16_t *gx,int16_t *gy,int16_t *gz)
{
	uint8_t buf[14];
	uint8_t i;

	MPU_IIC_Start();
	MPU_IIC_WriteByte(MPU6050_ADDR << 1);
	MPU_IIC_WriteByte(ACCEL_XOUT_H);

	MPU_IIC_Start();
	MPU_IIC_WriteByte((MPU6050_ADDR << 1)|0x01);

	for(i = 0; i < 14; i++)
	{
		if(i != 13)
		{
			buf[i] = MPU_IIC_ReadByte(0);
		}
		else
		{
			buf[i] = MPU_IIC_ReadByte(1);
		}
	}
	MPU_IIC_Stop();

	*ax = (int16_t)(buf[0] << 8 | buf[1]);
	*ay = (int16_t)(buf[2] << 8 | buf[3]);
	*az = (int16_t)(buf[4] << 8 | buf[5]);
	*gx = (int16_t)(buf[6] << 8 | buf[7]);
	*gy = (int16_t)(buf[8] << 8 | buf[9]);
	*gz = (int16_t)(buf[10] << 8 | buf[11]);
}

int16_t gx_off = 0, gy_off = 0, gz_off = 0;

void MPU6050_CalibrateGyro(void)
{
	int32_t sum_gx = 0, sum_gy = 0, sum_gz = 0;
	int16_t ax,ay,az,gx,gy,gz;
	uint16_t i;
	for(i = 0; i < 200; i++)
	{
		MPU6050_ReadAll(&ax,&ay,&az,&gx,&gy,&gz);
		sum_gx += gx;
		sum_gy += gy;
		sum_gz += gz;
		delay_ms(5);
	}
	gx_off = sum_gx / 200;
	gy_off = sum_gy / 200;
	gz_off = sum_gz / 200;
}