#ifndef __OLED_H
#define __OLED_H
#include "stm32f4xx.h"

void OLED_I2C_Init(void);
void OLED_WriteCommand(uint8_t Command);
void OLED_WriteData(uint8_t Data);
void OLED_SetCursor(uint8_t Y, uint8_t X);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);

/* ============== 中文 16x16 接口 ==============
 * Line    范围 1~4
 * Column  范围 1~15 (汉字占 2 列位 16 像素, 与 OLED_ShowChar 8px 列位统一)
 * index   范围 0 ~ OLED_CN_COUNT-1, 详见 oledfont_cn.h 顶部索引表 */
void OLED_ShowChinese(uint8_t Line, uint8_t Column, uint8_t index);

/* 中英混排 (GBK 编码, Keil 默认源编码):
 *   - 字节 < 0x80 -> ASCII 8x16, 步进 1 列位 (8 像素)
 *   - 字节 >= 0x80 + 下一字节 -> GBK 汉字 16x16, 步进 2 列位 (16 像素)
 *   - 列位单位 = 8 像素, 与 OLED_ShowChar/OLED_ShowChinese 一致
 *   - 字库未收录的汉字显示空白占位 (不报错, 不画框)
 *   - 行溢出时自动截断 */
void OLED_ShowMixedString(uint8_t Line, uint8_t Column, const char *str);

void OLED_Init(void);
uint32_t OLED_Pow(uint32_t X, uint32_t Y);

#endif