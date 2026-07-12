#ifndef __OLED_H
#define __OLED_H

#include "stm32f1xx_hal.h"
#include "main.h"  /* CubeMX 生成的引脚宏定义在这里 */

/* CubeMX 生成的宏名是 OLED_SCK_，OLED 驱动代码用的是 OLED_SCL_，需要映射 */
#define OLED_SCL_GPIO_Port  OLED_SCK_GPIO_Port
#define OLED_SCL_Pin        OLED_SCK_Pin
/* OLED_SDA_ 宏名一致，无需映射 */

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowChinese(uint8_t Line, uint8_t Column, uint8_t Index);

#endif
