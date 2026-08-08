#include "oled.h"
#include "oled_font.h"
#include "oled_cn.h"

#define OLED_W_SCL(x)   HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define OLED_W_SDA(x)   HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)

/* 软件 I2C 半周期延时次数：把 bit-bang 时钟压到 ~400kHz 规范以内。
   无延时直接翻转会跑 ~1MHz+，超过 SSD1306 400kHz 规范，偶发漏采沿
   导致字节边界错位 -> 状态机失锁（花屏/黑屏，只能断电恢复）。
   数值按屏线长短微调：仍偶发乱码就调大。 */
#define OLED_I2C_HALF_CLK  12

static void OLED_I2C_Delay(void)
{
    volatile uint8_t d;
    for (d = 0; d < OLED_I2C_HALF_CLK; d++) { }
}

void OLED_I2C_Init(void)
{
    OLED_W_SCL(1);
    OLED_W_SDA(1);
}

void OLED_I2C_Start(void)
{
    OLED_W_SDA(1);
    OLED_I2C_Delay();
    OLED_W_SCL(1);
    OLED_I2C_Delay();
    OLED_W_SDA(0);
    OLED_I2C_Delay();
    OLED_W_SCL(0);
    OLED_I2C_Delay();
}

void OLED_I2C_Stop(void)
{
    OLED_W_SDA(0);
    OLED_I2C_Delay();
    OLED_W_SCL(1);
    OLED_I2C_Delay();
    OLED_W_SDA(1);
    OLED_I2C_Delay();
}

void OLED_I2C_SendByte(uint8_t Byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        /* SDA 只在 SCL 低电平时变化：即使被中断打断，也不会在 SCL 高电平
           翻转 SDA 产生伪 START/STOP 条件，保证从机不会失步 */
        OLED_W_SDA(!!(Byte & (0x80 >> i)));
        OLED_I2C_Delay();
        OLED_W_SCL(1);
        OLED_I2C_Delay();
        OLED_W_SCL(0);
        OLED_I2C_Delay();
    }
    /* 第 9 个时钟：从机 ACK 位。本驱动不读 ACK，SDA 保持上一数据位电平，
       从机（开漏）主动拉低应答即可，无需释放 SDA，故不依赖外部上拉 */
    OLED_W_SCL(1);
    OLED_I2C_Delay();
    OLED_W_SCL(0);
    OLED_I2C_Delay();
}

void OLED_WriteCommand(uint8_t Command)
{
    OLED_I2C_Start();
    OLED_I2C_SendByte(0x78);
    OLED_I2C_SendByte(0x00);
    OLED_I2C_SendByte(Command);
    OLED_I2C_Stop();
}

void OLED_WriteData(uint8_t Data)
{
    OLED_I2C_Start();
    OLED_I2C_SendByte(0x78);
    OLED_I2C_SendByte(0x40);
    OLED_I2C_SendByte(Data);
    OLED_I2C_Stop();
}

void OLED_SetCursor(uint8_t Y, uint8_t X)
{
    OLED_WriteCommand(0xB0 | Y);
    OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));
    OLED_WriteCommand(0x00 | (X & 0x0F));
}

void OLED_Clear(void)
{
    uint8_t i, j;
    for (j = 0; j < 8; j++)
    {
        OLED_SetCursor(j, 0);
        for (i = 0; i < 128; i++)
        {
            OLED_WriteData(0x00);
        }
    }
}

void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
    uint8_t i;
    OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);
    for (i = 0; i < 8; i++)
    {
        OLED_WriteData(OLED_F8x16[Char - ' '][i]);
    }
    OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);
    for (i = 0; i < 8; i++)
    {
        OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]);
    }
}

void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
    uint8_t i;
    for (i = 0; String[i] != '\0'; i++)
    {
        OLED_ShowChar(Line, Column + i, String[i]);
    }
}

uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
    uint32_t Result = 1;
    while (Y--)
    {
        Result *= X;
    }
    return Result;
}

void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i;
    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0');
    }
}

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

void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i;
    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, Column + i, Number / OLED_Pow(2, Length - i - 1) % 2 + '0');
    }
}

/* 初始化命令序列：OLED_Init 与 OLED_Reinit 共用 */
static void OLED_InitSequence(void)
{
    OLED_I2C_Init();

    OLED_WriteCommand(0xAE);

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

    OLED_WriteCommand(0xAF);

    OLED_Clear();
}

void OLED_Init(void)
{
    HAL_Delay(200);
    OLED_InitSequence();
}

/* 显示失同步（花屏/黑屏）时的恢复入口：重发初始化序列即可复位 SSD1306 状态机。
   本屏无硬件复位脚、软件 I2C 不读 ACK，唯一的可靠恢复手段就是重发初始化。
   调用方（DisplayTask）在 SET 模式切换时调用，把"只能断电恢复"变成自动恢复。 */
void OLED_Reinit(void)
{
    OLED_InitSequence();
}

void OLED_ShowChinese(uint8_t Line, uint8_t Column, uint8_t Index)
{
    uint8_t i;
    OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);
    for (i = 0; i < 16; i++)
    {
        OLED_WriteData(OLED_CF16x16[Index][i]);
    }
    OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);
    for (i = 0; i < 16; i++)
    {
        OLED_WriteData(OLED_CF16x16[Index][i + 16]);
    }
}
