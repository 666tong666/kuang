#include "stm32f4xx.h"
#include "Delay.h"
#include "oledfont.h"
#include "oledfont_cn.h"

/* ========== 修改引脚：SCL PE4，SDA PE5 ========== */
#define OLED_W_SCL(x)		GPIO_WriteBit(GPIOE, GPIO_Pin_4, (BitAction)(x))
#define OLED_W_SDA(x)		GPIO_WriteBit(GPIOE, GPIO_Pin_5, (BitAction)(x))

/*引脚初始化 F407版本*/
void OLED_I2C_Init(void)
{
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStructure;
 	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;	//开漏输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
 	GPIO_Init(GPIOE, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
 	GPIO_Init(GPIOE, &GPIO_InitStructure);
	
	OLED_W_SCL(1);
	OLED_W_SDA(1);
}

/**
  * @brief  I2C开始
  * @param  无
  * @retval 无
  */
void OLED_I2C_Start(void)
{
	OLED_W_SDA(1);
	OLED_W_SCL(1);
	delay_us(5);
	OLED_W_SDA(0);
	delay_us(5);
	OLED_W_SCL(0);
}

/**
  * @brief  I2C停止
  * @param  无
  * @retval 无
  */
void OLED_I2C_Stop(void)
{
	OLED_W_SDA(0);
	OLED_W_SCL(1);
	delay_us(5);
	OLED_W_SDA(1);
}

/**
  * @brief  I2C发送一个字节
  * @param  Byte 要发送的一个字节
  * @retval 无
  */
void OLED_I2C_SendByte(uint8_t Byte)
{
	uint8_t i;
	for (i = 0; i < 8; i++)
	{
		OLED_W_SDA(Byte & (0x80 >> i));
		delay_us(5);
		OLED_W_SCL(1);
		delay_us(5);
		OLED_W_SCL(0);
		delay_us(5);
	}
	OLED_W_SCL(1);
	delay_us(5);
	OLED_W_SCL(0);
}

/**
  * @brief  OLED写命令
  * @param  Command 要写入的命令
  * @retval 无
  */
void OLED_WriteCommand(uint8_t Command)
{
	OLED_I2C_Start();
	OLED_I2C_SendByte(0x78);		//不显示换成0x7A
	OLED_I2C_SendByte(0x00);		//写命令
	OLED_I2C_SendByte(Command);
	OLED_I2C_Stop();
}

/**
  * @brief  OLED写数据
  * @param  Data 要写入的数据
  * @retval 无
  */
void OLED_WriteData(uint8_t Data)
{
	OLED_I2C_Start();
	OLED_I2C_SendByte(0x78);		//不显示换成0x7A
	OLED_I2C_SendByte(0x40);		//写数据
	OLED_I2C_SendByte(Data);
	OLED_I2C_Stop();
}

/**
  * @brief  OLED设置光标位置
  * @param  Y 以左上角为原点，向下方向的坐标，范围：0~7
  * @param  X 以左上角为原点，向右方向的坐标，范围：0~127
  * @retval 无
  */
void OLED_SetCursor(uint8_t Y, uint8_t X)
{
	OLED_WriteCommand(0xB0 | Y);					//设置Y位置
	OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));	//设置X位置高4位
	OLED_WriteCommand(0x00 | (X & 0x0F));			//设置X位置低4位
}

/**
  * @brief  OLED清屏
  * @param  无
  * @retval 无
  */
void OLED_Clear(void)
{
	uint8_t i, j;
	for (j = 0; j < 8; j++)
	{
		OLED_SetCursor(j, 0);
		for(i = 0; i < 128; i++)
		{
			OLED_WriteData(0x00);
		}
	}
}

/**
  * @brief  OLED显示一个字符
  * @param  Line 行位置，范围：1~4
  * @param  Column 列位置，范围：1~16
  * @param  Char 要显示的一个字符，范围：ASCII可见字符
  * @retval 无
  */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
	uint8_t i;
	OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);		//设置光标位置在上半部分
	for (i = 0; i < 8; i++)
	{
		OLED_WriteData(OLED_F8x16[Char - ' '][i]);			//显示上半部分内容
	}
	OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);	//设置光标位置在下半部分
	for (i = 0; i < 8; i++)
	{
		OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]);		//显示下半部分内容
	}
}

/**
  * @brief  OLED显示字符串
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  String 要显示的字符串，范围：ASCII可见字符
  * @retval 无
  */
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
	uint8_t i;
	for (i = 0; String[i] != '\0'; i++)
	{
		OLED_ShowChar(Line, Column + i, String[i]);
	}
}

/**
  * @brief  OLED次方函数
  * @retval 返回值等于X的Y次方
  */
uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;
	while (Y--)
	{
		Result *= X;
	}
	return Result;
}

/**
  * @brief  OLED显示数字（十进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~4294967295
  * @param  Length 要显示数字的长度，范围：1~10
  * @retval 无
  */
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
  * @brief  OLED显示数字（十进制，带符号数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：-2147483648~2147483647
  * @param  Length 要显示数字的长度，范围：1~10
  * @retval 无
  */
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
	uint8_t i;
	uint32_t Number1;
	if (Number >= 0)
	{
		OLED_ShowChar(Line, Column, '+');
		Number1 = Number;
	}
	else
	{
		OLED_ShowChar(Line, Column, '-');
		Number1 = -Number;
	}
	for (i = 0; i < Length; i++)
	{
		OLED_ShowChar(Line, Column + i + 1, Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
  * @brief  OLED显示数字（十六进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~0xFFFFFFFF
  * @param  Length 要显示数字的长度，范围：1~8
  * @retval 无
  */
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i, SingleNumber;
	for (i = 0; i < Length; i++)
	{
		SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;
		if (SingleNumber < 10)
		{
			OLED_ShowChar(Line, Column + i, SingleNumber + '0');
		}
		else
		{
			OLED_ShowChar(Line, Column + i, SingleNumber - 10 + 'A');
		}
	}
}

/**
  * @brief  OLED显示数字（二进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~1111 1111 1111 1111
  * @param  Length 要显示数字的长度，范围：1~16
  * @retval 无
  */
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(2, Length - i - 1) % 2 + '0');
	}
}

/**
  * @brief  OLED初始化
  * @param  无
  * @retval 无
  */
void OLED_Init(void)
{
	uint32_t i, j;

	for (i = 0; i < 1000; i++)
	{
		for (j = 0; j < 2000; j++);
	}

	OLED_I2C_Init();

	OLED_WriteCommand(0xAE);	//关闭显示

	OLED_WriteCommand(0xD5);
	OLED_WriteCommand(0x80);

	OLED_WriteCommand(0xA8);
	OLED_WriteCommand(0x3F);

	OLED_WriteCommand(0xD3);
	OLED_WriteCommand(0x00);

	OLED_WriteCommand(0x40);

	OLED_WriteCommand(0xA1);

	OLED_WriteCommand(0xC8);

	OLED_WriteCommand(0xDA);
	OLED_WriteCommand(0x12);

	OLED_WriteCommand(0x81);
	OLED_WriteCommand(0xCF);

	OLED_WriteCommand(0xD9);
	OLED_WriteCommand(0xF1);

	OLED_WriteCommand(0xDB);
	OLED_WriteCommand(0x30);

	OLED_WriteCommand(0xA4);

	OLED_WriteCommand(0xA6);

	OLED_WriteCommand(0x8D);
	OLED_WriteCommand(0x14);

	OLED_WriteCommand(0xAF);	//开启显示

	OLED_Clear();
}

/* ============== 中文 16x16 显示 ==============
 *
 * 字库布局: OLED_CN16x16[index] = { gbkh, gbkl, b0..b31 }
 *   byte[0..15]  = 上半 page (行 0..7), 每字节对应 1 列 8 像素 (bit0=顶)
 *   byte[16..31] = 下半 page (行 8..15), 布局同上半
 */
void OLED_ShowChinese(uint8_t Line, uint8_t Column, uint8_t index)
{
    uint8_t i;
    const uint8_t *p;

    if (Line < 1 || Line > 4)       return;
    if (Column < 1 || Column > 15)  return;     /* 占 16px = 2 个 8px 列位, 最大 15 */
    if (index >= OLED_CN_COUNT)     return;

    /* 指向该字的 32 字节字模起点 (跳过前 2 字节 GBK 头) */
    p = &OLED_CN16x16[index][2];

    /* 上半 page: 起始 = (Column-1)*8, 共写 16 列 */
    OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);
    for (i = 0; i < 16; i++)
    {
        OLED_WriteData(p[i]);
    }
    /* 下半 page */
    OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);
    for (i = 0; i < 16; i++)
    {
        OLED_WriteData(p[i + 16]);
    }
}

/**
 * @brief  按 GBK 二字节查字库下标; 找不到返回 0xFF
 */
static uint8_t OLED_FindChineseIndex(uint8_t gbk_h, uint8_t gbk_l)
{
    uint8_t i;
    for (i = 0; i < OLED_CN_COUNT; i++)
    {
        if (OLED_CN16x16[i][0] == gbk_h && OLED_CN16x16[i][1] == gbk_l)
        {
            return i;
        }
    }
    return 0xFF;
}

/**
 * @brief  中英混排字符串 (GBK 编码, 与 Keil 默认源编码一致)
 *         列位单位与 OLED_ShowChar 一致: 每列位 = 8 像素
 *         - 字节 < 0x80 -> ASCII 8x16, 占 1 列位
 *         - 字节 >= 0x80 + 下一字节 -> GBK 汉字 16x16, 占 2 列位
 *         - 行溢出自动截断; 未收录字留空 (不报错)
 */
void OLED_ShowMixedString(uint8_t Line, uint8_t Column, const char *str)
{
    uint8_t col = Column;       /* 8px 列位 */
    uint16_t i  = 0;

    if (Line < 1 || Line > 4) return;

    while (str[i] != '\0')
    {
        uint8_t b = (uint8_t)str[i];

        if (b < 0x80)
        {
            /* ASCII: 一行最多 16 个 8px 列位 */
            if (col > 16) break;
            OLED_ShowChar(Line, col, (char)b);
            col += 1;
            i++;
        }
        else
        {
            /* GBK 汉字首字节, 验证第二字节存在且合法 */
            uint8_t b2 = (uint8_t)str[i + 1];
            if (b2 == '\0' || b2 < 0x40)
            {
                i++;     /* 跳过首字节避免卡死 */
                continue;
            }
            /* 汉字占 2 列位, 最大起始列 = 15 */
            if (col > 15) break;

            uint8_t idx = OLED_FindChineseIndex(b, b2);
            if (idx != 0xFF)
            {
                OLED_ShowChinese(Line, col, idx);
            }
            /* 未收录的字: 留 2 列空白占位 (不画框) */

            col += 2;
            i += 2;
        }
    }
}