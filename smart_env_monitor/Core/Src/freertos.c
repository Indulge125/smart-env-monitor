/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"
#include "adc.h"
#include "tim.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ---- 硬件 / 协议常量：集中定义，避免魔法数字散布 ---- */
#define ADC_FULL_SCALE   4095  /* ADC 12 位满量程 */
#define SERVO_MIN_PULSE  500   /* 舵机最小脉宽(μs)，约 0° */
#define SERVO_MAX_PULSE  2500  /* 舵机最大脉宽(μs)，约 180° */
#define SERVO_MID_PULSE  1500  /* 舵机中位脉宽(μs)，约 90° */
#define SERVO_STEP       500   /* 手动模式步进脉宽(μs) */
#define SERVO_STEPS      5     /* 手动模式档位数量 */
#define LIGHT_THR_MIN    10    /* 光照阈值下界(%) */
#define LIGHT_THR_MAX    50    /* 光照阈值上界(%) */
#define LIGHT_THR_STEP   5     /* 光照阈值步进(%) */
#define LIGHT_THR_DEFAULT 30   /* 光照阈值初值(%) */
#define TEMP_THR_MIN     15    /* 温度阈值下界(°C) */
#define TEMP_THR_MAX     45    /* 温度阈值上界(°C) */
#define TEMP_THR_STEP    5     /* 温度阈值步进(°C) */
#define TEMP_THR_DEFAULT 30    /* 温度阈值初值(°C) */
#define VIN_REPORT_DV    33    /* 供电电压上报(×0.1V)，当前为占位值未实测 */
#define KEY_DEBOUNCE_MS  30    /* KEY_MODE 消抖窗口(ms)，与 KEY_SET 轮询消抖一致 */
#define KEY_POLL_MS      20    /* KeyTask 轮询周期(ms) */
#define SENSOR_SAMPLE_MS 1000  /* 传感器采样周期(ms)：1s 采样，5s 汇总上报 */
#define CONTROL_PERIOD_MS 200  /* ControlTask 报警控制周期(ms) */
#define DTU_TASK_PERIOD_MS 200 /* DtuTask 主循环周期(ms)，兼作 IWDG 喂狗间隔 */
#define DISPLAY_REFRESH_MS 300 /* DisplayTask OLED 刷新周期(ms) */
#define DISPLAY_INIT_HOLD_MS 100 /* OLED 初始化后等待(ms) */
#define RX_STATS_INTERVAL_MS 10000 /* 命令接收统计打印周期(ms) */
#define LIGHT_MAP_MAX_PCT 100.0f  /* 光照百分比线性映射上界(%) */
#define TEMP_MAP_MAX_C    50.0f   /* 温度线性映射上界(°C)：未标定，仅 ADC 线性估计（见 README 已知限制） */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* 传感器数据结构 */
typedef struct {
    uint16_t light_raw;
    uint16_t temp_raw;
    float light_pct;
    float temp_c;
} SensorData_t;

/* 全局传感器值（供其他模块读取） */
volatile float g_temp_value = 0.0f;
volatile float g_light_value = 0.0f;

/* 滑动均值滤波器 */
#define FILTER_SIZE 16
static uint16_t light_buf[FILTER_SIZE] = {0};
static uint16_t temp_buf[FILTER_SIZE] = {0};
static uint32_t light_sum = 0;
static uint32_t temp_sum = 0;
static uint8_t filter_idx = 0;
static uint8_t filter_cnt = 0;

/* 系统工作模式 */
typedef enum { MODE_AUTO = 0, MODE_MANUAL, MODE_SET } WorkMode_t;
#define WORK_MODE_COUNT ((int)MODE_SET + 1)  /* 模式总数，按键循环切换取模用 */
volatile WorkMode_t g_work_mode = MODE_AUTO;

/* 报警阈值 */
volatile uint8_t g_light_threshold = LIGHT_THR_DEFAULT;  /* 光照阈值(%) */
volatile uint8_t g_temp_threshold  = TEMP_THR_DEFAULT;   /* 温度阈值(°C) */
volatile uint8_t g_set_state = 0;         /* SET模式：0=调光照 1=调温度 2=保存退出 */

/* 舵机脉宽（500~2500 μs 对应约 0°~180°） */
volatile uint16_t g_servo_pulse = SERVO_MID_PULSE;

/* 信号量（在 MX_FREERTOS_Init 中创建） */
osSemaphoreId_t xSem_Key;
const osSemaphoreAttr_t xSem_Key_attr = { .name = "SemKey" };

/* 调试串口互斥量：Task1(传感器) 与 DtuTask(通信) 共用 huart2，防止输出交错 */
osMutexId_t xHuart2Mutex;
const osMutexAttr_t xHuart2Mutex_attr = { .name = "Huart2Mutex" };

/* ---- USART1（DTU）命令接收：环形缓冲，ISR 只写字节，DtuTask 组行消费。
     旧固定单缓冲的缺陷：① 连续两条指令到达时，第二条的 strncpy 会覆盖
     尚未被消费的第一条（丢命令）；② ISR 内做 256B strncpy 过长；
     ③ 缓冲满时静默清零，整帧丢弃无痕迹；④ 共享命令缓冲无 volatile。
     环形缓冲后：连续指令不丢帧、ISR 仅做几个字节的存取、溢出计数可观测。 ---- */
#define DTU_RX_RING_SIZE  256   /* 2 的幂，取模退化为掩码 */
#define DTU_RX_RING_MASK  (DTU_RX_RING_SIZE - 1)
static uint8_t  dtu_rx_byte;                    /* HAL_Receive_IT 的目标地址 */
static volatile uint8_t  dtu_rx_ring[DTU_RX_RING_SIZE];  /* ISR 写、任务读 */
static volatile uint16_t dtu_rx_head = 0;       /* 生产端（ISR） */
static volatile uint16_t dtu_rx_tail = 0;       /* 消费端（DtuTask） */
static volatile uint32_t dtu_rx_overflow = 0;   /* 环满丢字节计数（可观测） */
static volatile uint32_t dtu_rx_lines = 0;      /* 已处理命令行计数（可观测） */
static volatile uint32_t dtu_rx_arm_fail = 0;   /* ISR 重挂 Receive_IT 失败计数 */
static char dtu_cmd_line[DTU_RX_RING_SIZE];     /* 任务私有组行缓冲，无竞争 */
static uint16_t dtu_line_len = 0;

/* 收到指令的回显（验证用）：2026-08-08 连续下发 4 条指令验证通过
   （lines=4 overflow=0），已置 0 关闭。重新验证 PA9/PA10 接线时置 1：
   ① 打到调试口 [DTU] RX: xxx；
   ② 原样回发 USART1(PA9)，助手 RX 接 PA9 时直接看到指令弹回来，
      证明 助手TX→PA10 与 PA9→助手RX 双向链路都通。 */
#define DTU_RX_ECHO 0

/* ---- USART2（调试口）指令通道：同一套环形缓冲 + 行组装，复用 DTU_ParseCommand。
     一根 USB-TTL 接 PA2/PA3 即可同时看日志 + 发指令；PA9/PA10 保持专属于 DTU，
     避免两根 TX 打架。命令回显 [HOST] RX: xxx 直接打在调试口，同一窗口可见。 ---- */
#define HOST_RX_RING_SIZE  DTU_RX_RING_SIZE
#define HOST_RX_RING_MASK  (HOST_RX_RING_SIZE - 1)
static uint8_t  host_rx_byte;                    /* HAL_Receive_IT 的目标地址 */
static volatile uint8_t  host_rx_ring[HOST_RX_RING_SIZE];
static volatile uint16_t host_rx_head = 0;       /* 生产端（USART2 ISR） */
static volatile uint16_t host_rx_tail = 0;       /* 消费端（DtuTask） */
static volatile uint32_t host_rx_overflow = 0;   /* 环满丢字节计数（可观测） */
static volatile uint32_t host_rx_lines = 0;      /* 已处理命令行计数（可观测） */
static volatile uint32_t host_rx_arm_fail = 0;   /* ISR 重挂 Receive_IT 失败计数 */
static char host_cmd_line[HOST_RX_RING_SIZE];    /* 任务私有组行缓冲，无竞争 */
static uint16_t host_line_len = 0;

/* 心跳上报周期(ms)：DTU 链路健康由平台侧按上报新鲜度判定（5s 上报，
   网关侧 15s 未更新判离线），无硬件断连检测——RDY/RST 引脚（PB10/PB11）未接线。
   旧"断连自动复位"逻辑因 DTU_IsConnected 恒返回 1 而不可达，属死代码，已删除。 */
#define DTU_REPORT_INTERVAL 5000

#define SENSOR_DEBUG_INTERVAL 5000
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for SensorTask */
osThreadId_t SensorTaskHandle;
const osThreadAttr_t SensorTask_attributes = {
  .name = "SensorTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for DisplayTask */
osThreadId_t DisplayTaskHandle;
const osThreadAttr_t DisplayTask_attributes = {
  .name = "DisplayTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for ControlTask */
osThreadId_t ControlTaskHandle;
const osThreadAttr_t ControlTask_attributes = {
  .name = "ControlTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for DtuTask（任务名与硬件职责一致：驱动 4G DTU） */
osThreadId_t DtuTaskHandle;
const osThreadAttr_t DtuTask_attributes = {
  .name = "DtuTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for KeyTask */
osThreadId_t KeyTaskHandle;
const osThreadAttr_t KeyTask_attributes = {
  .name = "KeyTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for xSensorQueue */
osMessageQueueId_t xSensorQueueHandle;
const osMessageQueueAttr_t xSensorQueue_attributes = {
  .name = "xSensorQueue"
};
/* Definitions for xKeyQueue */
osMessageQueueId_t xKeyQueueHandle;
const osMessageQueueAttr_t xKeyQueue_attributes = {
  .name = "xKeyQueue"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static uint16_t SensorFilter(uint16_t new_val, uint16_t *buf, uint32_t *sum);
static uint16_t ADC_ReadChannel(uint32_t channel);
static void UART2_Print(const char *msg);
static void PrintStackWatermarks(void);
static void DTU_Init(void);
static void DTU_SendData(const char *data);
static void DTU_SendTelemetry(const char *did, uint8_t debug_log);
static void DTU_ParseCommand(const char *cmd);
static void DTU_ProcessRx(void);
static void HOST_Init(void);
static void HOST_ProcessRx(void);
static void IWDG_Init(void);
static void IWDG_Feed(void);

/* 验证用：向串口发 CRASH 模拟死机，IWDG 约 2s 后自动复位（上板验证看门狗，
   已于 2026-08-08 两次验证通过）。终版置 0 关闭——测试钩子不进交付固件；
   需复验时翻回 1 重新编译烧录即可。 */
#define WDT_CRASH_TEST 0
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void Task1(void *argument);
void Task2(void *argument);
void Task3(void *argument);
void Task4(void *argument);
void Task5(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  xHuart2Mutex = osMutexNew(&xHuart2Mutex_attr);
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  xSem_Key = osSemaphoreNew(1, 0, &xSem_Key_attr);
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of xSensorQueue */
  xSensorQueueHandle = osMessageQueueNew (5, 8, &xSensorQueue_attributes);

  /* creation of xKeyQueue */
  xKeyQueueHandle = osMessageQueueNew (3, 4, &xKeyQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of SensorTask */
  SensorTaskHandle = osThreadNew(Task1, NULL, &SensorTask_attributes);

  /* creation of DisplayTask */
  DisplayTaskHandle = osThreadNew(Task2, NULL, &DisplayTask_attributes);

  /* creation of ControlTask */
  ControlTaskHandle = osThreadNew(Task3, NULL, &ControlTask_attributes);

  /* creation of DtuTask */
  DtuTaskHandle = osThreadNew(Task4, NULL, &DtuTask_attributes);

  /* creation of KeyTask */
  KeyTaskHandle = osThreadNew(Task5, NULL, &KeyTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_Task1 */
/**
* @brief Function implementing the SensorTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Task1 */
void Task1(void *argument)
{
  /* USER CODE BEGIN Task1 */
  SensorData_t data;
  uint32_t last_debug = 0;

  for (;;)
  {
    data.light_raw = ADC_ReadChannel(ADC_CHANNEL_0);
    data.temp_raw  = ADC_ReadChannel(ADC_CHANNEL_1);

    if (filter_cnt < FILTER_SIZE) filter_cnt++;
    data.light_raw = SensorFilter(data.light_raw, light_buf, &light_sum);
    data.temp_raw  = SensorFilter(data.temp_raw,  temp_buf,  &temp_sum);
    filter_idx = (filter_idx + 1) % FILTER_SIZE;

    data.light_pct = LIGHT_MAP_MAX_PCT - ((float)data.light_raw / (float)ADC_FULL_SCALE * LIGHT_MAP_MAX_PCT);
    data.temp_c = TEMP_MAP_MAX_C - ((float)data.temp_raw / (float)ADC_FULL_SCALE * TEMP_MAP_MAX_C);

    g_light_value = data.light_pct;
    g_temp_value  = data.temp_c;

    if ((HAL_GetTick() - last_debug) >= SENSOR_DEBUG_INTERVAL)
    {
      char debug_json[160];
      int l = (int)(g_light_value + 0.5f);
      int t = (int)(g_temp_value + 0.5f);
      snprintf(debug_json, sizeof(debug_json),
               "[SENSOR] {\"light\":%d,\"temp\":%d,\"mode\":%d,\"servo\":%d}\r\n",
               l, t, (int)g_work_mode, (int)g_servo_pulse);
      UART2_Print(debug_json);
      PrintStackWatermarks();
      last_debug = HAL_GetTick();
    }

    osDelay(SENSOR_SAMPLE_MS);
  }
  /* USER CODE END Task1 */
}

/* USER CODE BEGIN Header_Task2 */
/**
* @brief Function implementing the DisplayTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Task2 */
void Task2(void *argument)
{
  /* USER CODE BEGIN Task2 */
  char line[17];
  WorkMode_t last_mode = (WorkMode_t)-1;
  int last_light = -1, last_temp = -1;
  uint8_t last_set_state = 0xFF;
  uint8_t last_lt = 0xFF, last_tt = 0xFF;

  OLED_Clear();
  osDelay(DISPLAY_INIT_HOLD_MS);

  for (;;)
  {
    /* 模式跨越 SET 边界时重发 OLED 初始化：复位可能已失同步的 SSD1306
       （花屏/黑屏自愈，免去断电重启）。首轮 last_mode=0xFF 与 AUTO 同为
       "非SET"，不会触发。 */
    if ((last_mode == MODE_SET) != (g_work_mode == MODE_SET))
    {
        OLED_Reinit();
    }

    if (g_work_mode == MODE_SET)
    {
        if (last_mode != MODE_SET || last_set_state != g_set_state ||
            last_lt != g_light_threshold || last_tt != g_temp_threshold)
        {
            /* 每行固定 16 字符全宽覆盖，杜绝模式切换后残留旧字符（如 "Bright:96%%"） */
            snprintf(line, sizeof(line), "Light Thr:%2d%%   ", g_light_threshold);
            vTaskSuspendAll();  /* 防止高优先级任务在 bit-bang I2C 中途抢占导致断帧 */
            OLED_ShowString(1, 1, line);
            xTaskResumeAll();

            snprintf(line, sizeof(line), "Temp Thr:%2dC    ", g_temp_threshold);
            vTaskSuspendAll();
            OLED_ShowString(2, 1, line);
            xTaskResumeAll();

            switch (g_set_state)
            {
                case 0:  OLED_ShowString(3, 1, "Set Light Thr   "); break;
                case 1:  OLED_ShowString(3, 1, "Set Temp Thr    "); break;
                case 2:  OLED_ShowString(3, 1, "Save&Exit       "); break;
            }
            last_set_state = g_set_state;
            last_lt = g_light_threshold;
            last_tt = g_temp_threshold;
        }
    }
    else
    {
        int light_int = (int)(g_light_value + 0.5f);
        int temp_int = (int)g_temp_value;

        if (last_mode != g_work_mode || last_light != light_int)
        {
            snprintf(line, sizeof(line), "Bright:%3d%%     ", light_int);
            vTaskSuspendAll();
            OLED_ShowString(1, 1, line);
            xTaskResumeAll();
            last_light = light_int;
        }

        if (last_mode != g_work_mode || last_temp != temp_int)
        {
            snprintf(line, sizeof(line), "Temp:   %2dC     ", temp_int);
            vTaskSuspendAll();
            OLED_ShowString(2, 1, line);
            xTaskResumeAll();
            last_temp = temp_int;
        }

        if (last_mode != g_work_mode)
        {
            switch (g_work_mode)
            {
                case MODE_AUTO:   OLED_ShowString(3, 1, "Mode:   Auto    "); break;
                case MODE_MANUAL: OLED_ShowString(3, 1, "Mode:   Manual  "); break;
                default: break;
            }
        }

        last_set_state = 0xFF;
        last_lt = 0xFF;
        last_tt = 0xFF;
    }

    last_mode = g_work_mode;
    osDelay(DISPLAY_REFRESH_MS);
  }
  /* USER CODE END Task2 */
}

/* USER CODE BEGIN Header_Task3 */
/**
* @brief Function implementing the ControlTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Task3 */
void Task3(void *argument)
{
  /* USER CODE BEGIN Task3 */
  for (;;)
  {
    if (g_work_mode == MODE_AUTO)
    {
        float light = g_light_value;
        float temp  = g_temp_value;

        /* 光照报警：PA11 + 舵机 */
        if (light < (float)g_light_threshold)
        {
            HAL_GPIO_WritePin(LED_ALARM_GPIO_Port, LED_ALARM_Pin, GPIO_PIN_RESET);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, SERVO_MAX_PULSE);
        }
        else
        {
            HAL_GPIO_WritePin(LED_ALARM_GPIO_Port, LED_ALARM_Pin, GPIO_PIN_SET);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, SERVO_MIN_PULSE);
        }

        /* 温度报警：PA7 */
        if (temp > (float)g_temp_threshold)
        {
            HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_RESET);
        }
        else
        {
            HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_SET);
        }
    }
    else if (g_work_mode == MODE_MANUAL)
    {
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, g_servo_pulse);
    }

    osDelay(CONTROL_PERIOD_MS);
  }
  /* USER CODE END Task3 */
}

/* USER CODE BEGIN Header_Task4 */
/**
* @brief Function implementing the DtuTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Task4 */
void Task4(void *argument)
{
  /* USER CODE BEGIN Task4 */
  uint32_t last_report = 0;
  uint32_t last_rx_stat = 0;

  DTU_Init();
  HOST_Init();
  IWDG_Init();   /* 看门狗最后挂载：此后任何死循环/任务饿死都触发硬件复位 */

  for (;;)
  {
    IWDG_Feed();   /* 喂狗：200ms 周期 << 2s 超时，留足 10 倍余量 */
    DTU_ProcessRx();
    HOST_ProcessRx();

    /* 每 10s 打印命令接收统计：连续下发指令后核对 lines 递增、overflow=0，
       是环形缓冲"不丢帧"的板上可观测证据 */
    if ((HAL_GetTick() - last_rx_stat) >= RX_STATS_INTERVAL_MS)
    {
      char stat[200];
      snprintf(stat, sizeof(stat),
               "[DTU] RX lines=%lu overflow=%lu arm_fail=%lu | "
               "[HOST] RX lines=%lu overflow=%lu arm_fail=%lu\r\n",
               (unsigned long)dtu_rx_lines, (unsigned long)dtu_rx_overflow,
               (unsigned long)dtu_rx_arm_fail,
               (unsigned long)host_rx_lines, (unsigned long)host_rx_overflow,
               (unsigned long)host_rx_arm_fail);
      UART2_Print(stat);
      last_rx_stat = HAL_GetTick();
    }

    /* 心跳上报：DTU 链路健康由平台侧按上报新鲜度判定（见 DTU_REPORT_INTERVAL 注释），
       无硬件 RDY/RST 断连检测——原"断连自动复位"死逻辑已删除 */
    if ((HAL_GetTick() - last_report) >= DTU_REPORT_INTERVAL)
    {
      DTU_SendTelemetry("0", 1);
      last_report = HAL_GetTick();
    }

    osDelay(DTU_TASK_PERIOD_MS);
  }
  /* USER CODE END Task4 */
}

/* USER CODE BEGIN Header_Task5 */
/**
* @brief Function implementing the KeyTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Task5 */
void Task5(void *argument)
{
  /* USER CODE BEGIN Task5 */
  uint8_t pb12_prev = 1;
  uint8_t servo_idx = 0;

  for (;;)
  {
    /* ---- KEY_MODE (PA4)：EXTI 中断 + 任务内消抖 ---- */
    if (osSemaphoreAcquire(xSem_Key, 10) == osOK)
    {
        /* 消抖：等信号稳定后重读引脚，确认按键确实按下 */
        osDelay(KEY_DEBOUNCE_MS);
        if (HAL_GPIO_ReadPin(KEY_MODE_GPIO_Port, KEY_MODE_Pin) == GPIO_PIN_RESET)
        {
            if (g_work_mode == MODE_SET)
            {
                /* SET模式：PA4 减小当前阈值 */
                if (g_set_state == 0)
                {
                    if (g_light_threshold <= LIGHT_THR_MIN) g_light_threshold = LIGHT_THR_MAX;
                    else g_light_threshold -= LIGHT_THR_STEP;
                }
                else if (g_set_state == 1)
                {
                    if (g_temp_threshold <= TEMP_THR_MIN) g_temp_threshold = TEMP_THR_MAX;
                    else g_temp_threshold -= TEMP_THR_STEP;
                }
            }
            else
            {
                g_work_mode = (WorkMode_t)(((int)g_work_mode + 1) % WORK_MODE_COUNT);
                if (g_work_mode == MODE_MANUAL)
                {
                    servo_idx = 0;
                    g_servo_pulse = SERVO_MIN_PULSE;
                }
                else if (g_work_mode == MODE_SET)
                {
                    g_set_state = 0;
                }
            }
        }
        /* 排空消抖窗口内积累的信号量，避免抖动被计为多次按键 */
        while (osSemaphoreAcquire(xSem_Key, 0) == osOK) {}
    }

    /* ---- KEY_SET (PB12)：轮询边沿检测 ---- */
    uint8_t pb12_now = HAL_GPIO_ReadPin(KEY_SET_GPIO_Port, KEY_SET_Pin);
    if (pb12_prev == 1 && pb12_now == 0)
    {
        osDelay(KEY_DEBOUNCE_MS);
        if (HAL_GPIO_ReadPin(KEY_SET_GPIO_Port, KEY_SET_Pin) == 0)
        {
            if (g_work_mode == MODE_MANUAL)
            {
                servo_idx = (servo_idx + 1) % SERVO_STEPS;
                g_servo_pulse = SERVO_MIN_PULSE + servo_idx * SERVO_STEP;
            }
            else if (g_work_mode == MODE_SET)
            {
                if (g_set_state == 0)
                {
                    g_light_threshold += LIGHT_THR_STEP;
                    if (g_light_threshold > LIGHT_THR_MAX) g_light_threshold = LIGHT_THR_MIN;
                }
                else if (g_set_state == 1)
                {
                    g_temp_threshold += TEMP_THR_STEP;
                    if (g_temp_threshold > TEMP_THR_MAX) g_temp_threshold = TEMP_THR_MIN;
                }
                else
                {
                    g_work_mode = MODE_AUTO;
                    g_set_state = 0;
                }
                if (g_work_mode == MODE_SET)
                {
                    g_set_state++;
                    if (g_set_state > 2) g_set_state = 0;
                }
            }
        }
    }
    pb12_prev = pb12_now;

    osDelay(KEY_POLL_MS);
  }
  /* USER CODE END Task5 */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* 调试串口输出：互斥保护，避免 Task1 与 DtuTask 输出交错 */
static void UART2_Print(const char *msg)
{
  if (msg == NULL)
  {
    return;
  }
  if (xHuart2Mutex != NULL)
  {
    osMutexAcquire(xHuart2Mutex, osWaitForever);
  }
  HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), 100);
  if (xHuart2Mutex != NULL)
  {
    osMutexRelease(xHuart2Mutex);
  }
}

/* 周期打印各任务栈剩余量（osThreadGetStackSpace 内部 = uxTaskGetStackHighWaterMark × 4 字节） */
static void PrintStackWatermarks(void)
{
  char line[128];
  snprintf(line, sizeof(line),
           "[STACK] Sensor:%4uB Dsp:%4uB Ctl:%4uB Dtu:%4uB Key:%4uB Def:%4uB\r\n",
           (unsigned)osThreadGetStackSpace(SensorTaskHandle),
           (unsigned)osThreadGetStackSpace(DisplayTaskHandle),
           (unsigned)osThreadGetStackSpace(ControlTaskHandle),
           (unsigned)osThreadGetStackSpace(DtuTaskHandle),
           (unsigned)osThreadGetStackSpace(KeyTaskHandle),
           (unsigned)osThreadGetStackSpace(defaultTaskHandle));
  UART2_Print(line);
}

static void DTU_Init(void)
{
  HAL_GPIO_WritePin(DTU_RST_GPIO_Port, DTU_RST_Pin, GPIO_PIN_RESET);
  dtu_rx_head = 0;
  dtu_rx_tail = 0;
  dtu_rx_overflow = 0;
  dtu_rx_lines = 0;
  dtu_rx_arm_fail = 0;
  dtu_line_len = 0;
  if (HAL_UART_Receive_IT(&huart1, &dtu_rx_byte, 1) != HAL_OK)
  {
    UART2_Print("[ERR] USART1 RX arm failed\r\n");
  }

  const char *msg = "[DTU] init done\r\n";
  UART2_Print(msg);
}

static void DTU_SendData(const char *data)
{
  if (data == NULL)
  {
    return;
  }
  /* DTU 未接电/线断时 Transmit 会阻塞至超时返回 HAL_BUSY，必须检查而非静默丢弃 */
  if (HAL_UART_Transmit(&huart1, (uint8_t *)data, strlen(data), 500) != HAL_OK)
  {
    UART2_Print("[ERR] USART1 TX data failed\r\n");
    return;
  }
  if (HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 100) != HAL_OK)
  {
    UART2_Print("[ERR] USART1 TX terminator failed\r\n");
  }
}

static void DTU_SendTelemetry(const char *did, uint8_t debug_log)
{
  char json[256];
  const char *use_did = (did != NULL && did[0] != '\0') ? did : "0";
  int l = (int)(g_light_value + 0.5f);
  int t = (int)(g_temp_value + 0.5f);
  int m = (int)g_work_mode;
  int sw1 = (g_work_mode == MODE_MANUAL) ? 1 : 0;
  int in1 = (g_temp_value > (float)g_temp_threshold) ? 1 : 0;
  int vin = VIN_REPORT_DV;
  unsigned long ts = (unsigned long)(HAL_GetTick() / 1000U);

  snprintf(json, sizeof(json),
           "{\"cmd\":\"dup\",\"did\":\"%s\",\"times\":\"%lu000\","
           "\"param\":{\"light\":%d,\"temp\":%d,\"mode\":%d,"
           "\"sw1\":%d,\"in1\":%d,\"vin\":%d}}",
           use_did, ts, l, t, m, sw1, in1, vin);

  DTU_SendData(json);

  if (debug_log)
  {
    char dbg[300];
    snprintf(dbg, sizeof(dbg), "[DTU] TX dup: %s\r\n", json);
    UART2_Print(dbg);
  }
}

static void DTU_ParseCommand(const char *cmd)
{
  if (cmd == NULL)
  {
    return;
  }

  if (strstr(cmd, "\"cmd\":\"sget\"") || strstr(cmd, "\"cmd\": \"sget\""))
  {
    const char *msg = "[DTU] RX sget ignored, auto report only\r\n";
    UART2_Print(msg);
  }
  else if (strstr(cmd, "SERVO="))
  {
    int pulse = atoi(strstr(cmd, "SERVO=") + 6);
    if (pulse < SERVO_MIN_PULSE) pulse = SERVO_MIN_PULSE;
    if (pulse > SERVO_MAX_PULSE) pulse = SERVO_MAX_PULSE;
    g_servo_pulse = (uint16_t)pulse;
    g_work_mode = MODE_MANUAL;
  }
  else if (strstr(cmd, "MODE=AUTO"))
  {
    g_work_mode = MODE_AUTO;
  }
  else if (strstr(cmd, "MODE=MANUAL"))
  {
    g_work_mode = MODE_MANUAL;
  }
  else if (strstr(cmd, "LED=ON"))
  {
    HAL_GPIO_WritePin(LED_ALARM_GPIO_Port, LED_ALARM_Pin, GPIO_PIN_RESET);
  }
  else if (strstr(cmd, "LED=OFF"))
  {
    HAL_GPIO_WritePin(LED_ALARM_GPIO_Port, LED_ALARM_Pin, GPIO_PIN_SET);
  }
#if WDT_CRASH_TEST
  else if (strstr(cmd, "CRASH"))
  {
    UART2_Print("[WDT] deliberate crash, IWDG resets in ~2s\r\n");
    __disable_irq();
    while (1)
    {
    }
  }
#endif
}

/* 消费 USART1 环形缓冲并组装命令行：任务私有缓冲，ISR 只写环。
   \n 或 \r 视为行结束；空行忽略；超长行整行丢弃（防半帧假命令）。 */
static void DTU_ProcessRx(void)
{
  while (dtu_rx_tail != dtu_rx_head)
  {
    uint8_t ch = dtu_rx_ring[dtu_rx_tail];
    dtu_rx_tail = (uint16_t)((dtu_rx_tail + 1) & DTU_RX_RING_MASK);

    if (ch == '\n' || ch == '\r')
    {
      if (dtu_line_len > 0)
      {
        dtu_cmd_line[dtu_line_len] = '\0';
#if DTU_RX_ECHO
        UART2_Print("[DTU] RX: ");
        UART2_Print(dtu_cmd_line);
        UART2_Print("\r\n");
        /* 原样回发 USART1：验证接线时，助手端直接看到自己发的指令回来 */
        DTU_SendData(dtu_cmd_line);
#endif
        DTU_ParseCommand(dtu_cmd_line);
        dtu_rx_lines++;
        dtu_line_len = 0;
      }
      continue;
    }

    if (dtu_line_len < sizeof(dtu_cmd_line) - 1)
    {
      dtu_cmd_line[dtu_line_len++] = (char)ch;
    }
    else
    {
      dtu_line_len = 0;   /* 超长：丢弃整行，不解析半帧 */
    }
  }
}

/* USART2（调试口）指令通道初始化：清零计数并挂起单字节中断接收 */
static void HOST_Init(void)
{
  host_rx_head = 0;
  host_rx_tail = 0;
  host_rx_overflow = 0;
  host_rx_lines = 0;
  host_rx_arm_fail = 0;
  host_line_len = 0;
  if (HAL_UART_Receive_IT(&huart2, &host_rx_byte, 1) != HAL_OK)
  {
    UART2_Print("[ERR] USART2 RX arm failed\r\n");
  }
}

/* 消费 USART2 环形缓冲并组装命令行：与 DTU 通道同构，命令解析复用。
   回显 [HOST] RX: xxx 直接打在调试口——同一窗口可见，无需换线。 */
static void HOST_ProcessRx(void)
{
  while (host_rx_tail != host_rx_head)
  {
    uint8_t ch = host_rx_ring[host_rx_tail];
    host_rx_tail = (uint16_t)((host_rx_tail + 1) & HOST_RX_RING_MASK);

    if (ch == '\n' || ch == '\r')
    {
      if (host_line_len > 0)
      {
        host_cmd_line[host_line_len] = '\0';
        UART2_Print("[HOST] RX: ");
        UART2_Print(host_cmd_line);
        UART2_Print("\r\n");
        DTU_ParseCommand(host_cmd_line);
        host_rx_lines++;
        host_line_len = 0;
      }
      continue;
    }

    if (host_line_len < sizeof(host_cmd_line) - 1)
    {
      host_cmd_line[host_line_len++] = (char)ch;
    }
    else
    {
      host_line_len = 0;   /* 超长：丢弃整行，不解析半帧 */
    }
  }
}

/* ---- IWDG 看门狗：LSI 独立时钟驱动，不复位、不失效；任何任务死循环/饿死
     超过超时即硬件复位（取代 Error_Handler 的 while(1) 永久死等）。
     寄存器级实现（F1 HAL 的 IWDG 模块未启用）：
     KR=0x5555 解锁 → 写 PR/RLR → KR=0xAAAA 重装 → KR=0xCCCC 启动；喂狗写 KR=0xAAAA。
     超时 = (PR=64 ÷ LSI 40kHz) × RLR=1250 ≈ 2s（LSI 实际 30~60kHz，约 1.5~3s）。 ---- */
static void IWDG_Init(void)
{
  __HAL_RCC_LSI_ENABLE();
  while ((RCC->CSR & RCC_CSR_LSIRDY) == 0U)
  {
  }

  IWDG->KR = 0x5555U;   /* 解锁写保护 */
  IWDG->PR = 0x4U;      /* 预分频 /64 → 1.6ms/计数（40kHz 标称） */
  IWDG->RLR = 1250U;    /* 约 2s 超时 */
  IWDG->KR = 0xAAAAU;   /* 装载重载值 */
  IWDG->KR = 0xCCCCU;   /* 启动 */
  UART2_Print("[WDT] IWDG armed, ~2s timeout\r\n");
}

static void IWDG_Feed(void)
{
  IWDG->KR = 0xAAAAU;   /* 重新装载计数器 */
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    uint16_t next = (uint16_t)((dtu_rx_head + 1) & DTU_RX_RING_MASK);
    if (next != dtu_rx_tail)
    {
      dtu_rx_ring[dtu_rx_head] = dtu_rx_byte;
      dtu_rx_head = next;
    }
    else
    {
      dtu_rx_overflow++;   /* 环满：丢新字节并计数，不再静默覆盖旧命令 */
    }

    if (HAL_UART_Receive_IT(&huart1, &dtu_rx_byte, 1) != HAL_OK)
    {
      dtu_rx_arm_fail++;   /* 重挂失败：接收将静默停止，必须可观测 */
    }
  }
  else if (huart->Instance == USART2)
  {
    uint16_t next = (uint16_t)((host_rx_head + 1) & HOST_RX_RING_MASK);
    if (next != host_rx_tail)
    {
      host_rx_ring[host_rx_head] = host_rx_byte;
      host_rx_head = next;
    }
    else
    {
      host_rx_overflow++;   /* 环满：丢新字节并计数 */
    }

    if (HAL_UART_Receive_IT(&huart2, &host_rx_byte, 1) != HAL_OK)
    {
      host_rx_arm_fail++;   /* 重挂失败：接收将静默停止，必须可观测 */
    }
  }
}

/* ---- 辅助函数 ---- */

/**
  * @brief 滑动均值滤波器
  */
static uint16_t SensorFilter(uint16_t new_val, uint16_t *buf, uint32_t *sum)
{
    *sum -= buf[filter_idx];
    buf[filter_idx] = new_val;
    *sum += new_val;
    return (uint16_t)(*sum / filter_cnt);
}

/**
  * @brief 读取指定ADC通道的原始值
  */
static uint16_t ADC_ReadChannel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    static uint16_t last_value[2] = {0, 0};  /* 按通道保留上次成功值，转换失败时回退 */
    uint8_t ch = (channel == ADC_CHANNEL_1) ? 1 : 0;

    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        UART2_Print("[ERR] ADC config failed\r\n");
        return last_value[ch];
    }
    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        UART2_Print("[ERR] ADC start failed\r\n");
        return last_value[ch];
    }
    if (HAL_ADC_PollForConversion(&hadc1, 100) != HAL_OK)
    {
        HAL_ADC_Stop(&hadc1);
        UART2_Print("[ERR] ADC conv timeout\r\n");
        return last_value[ch];
    }
    last_value[ch] = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return last_value[ch];
}

/* USER CODE END Application */
