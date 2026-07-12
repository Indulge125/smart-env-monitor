#include "delay.h"

void Delay_us(uint32_t us)
{
    uint32_t ticks = us * (SystemCoreClock / 1000000) / 5;
    while (ticks--)
    {
        __NOP();
    }
}

void Delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

void Delay_s(uint32_t s)
{
    while (s--)
    {
        HAL_Delay(1000);
    }
}
