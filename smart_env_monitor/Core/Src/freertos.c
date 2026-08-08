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

#define DTU_RX_BUF_SIZE 256
static uint8_t dtu_rx_byte;
static uint8_t dtu_rx_buf[DTU_RX_BUF_SIZE];
static volatile uint16_t dtu_rx_idx = 0;
static volatile uint8_t dtu_connected = 0;

#define DTU_USE_RDY_PIN 0
#define CMD_BUF_SIZE 256
static char dtu_cmd_buf[CMD_BUF_SIZE];
static volatile uint8_t dtu_cmd_ready = 0;

#define DTU_REPORT_INTERVAL 5000
#define DTU_DISCONNECT_LIMIT 2
#define DTU_RST_HOLD_MS 1200
static uint8_t dtu_disconnect_cnt = 0;

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
osThreadId_t WifiTaskHandle;
const osThreadAttr_t WifiTask_attributes = {
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
static uint8_t DTU_IsConnected(void);
static void DTU_SendData(const char *data);
static void DTU_SendTelemetry(const char *did, uint8_t debug_log);
static void DTU_ParseCommand(const char *cmd);
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

  /* creation of WifiTask */
  WifiTaskHandle = osThreadNew(Task4, NULL, &WifiTask_attributes);

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

    data.light_pct = 100.0f - ((float)data.light_raw / (float)ADC_FULL_SCALE * 100.0f);
    data.temp_c = 50.0f - ((float)data.temp_raw / (float)ADC_FULL_SCALE * 50.0f);

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

    osDelay(1000);
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
  char line1[17], line2[17];
  WorkMode_t last_mode = (WorkMode_t)-1;
  int last_light = -1, last_temp = -1;
  uint8_t last_set_state = 0xFF;
  uint8_t last_lt = 0xFF, last_tt = 0xFF;

  OLED_Clear();
  osDelay(100);

  for (;;)
  {
    if (g_work_mode == MODE_SET)
    {
        if (last_mode != MODE_SET || last_set_state != g_set_state ||
            last_lt != g_light_threshold || last_tt != g_temp_threshold)
        {
            snprintf(line1, sizeof(line1), "Light Thr:%2d%%", g_light_threshold);
            OLED_ShowString(1, 1, line1);

            snprintf(line2, sizeof(line2), "Temp Thr:%2dC ", g_temp_threshold);
            OLED_ShowString(2, 1, line2);

            switch (g_set_state)
            {
                case 0:  OLED_ShowString(3, 1, "Set Light Thr  "); break;
                case 1:  OLED_ShowString(3, 1, "Set Temp Thr   "); break;
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
            snprintf(line1, sizeof(line1), "Bright:%3d%%", light_int);
            OLED_ShowString(1, 1, line1);
            last_light = light_int;
        }

        if (last_mode != g_work_mode || last_temp != temp_int)
        {
            snprintf(line2, sizeof(line2), "Temp:   %2dC ", temp_int);
            OLED_ShowString(2, 1, line2);
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
    osDelay(300);
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

    osDelay(200);
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

  DTU_Init();
  dtu_connected = DTU_IsConnected();

  for (;;)
  {
    if (dtu_cmd_ready)
    {
      dtu_cmd_ready = 0;
      DTU_ParseCommand(dtu_cmd_buf);
    }

    dtu_connected = DTU_IsConnected();
    if (!dtu_connected)
    {
      dtu_disconnect_cnt++;
      if (dtu_disconnect_cnt >= DTU_DISCONNECT_LIMIT)
      {
        const char *msg = "[DTU] disconnected too long, resetting...\r\n";
        UART2_Print(msg);

        HAL_GPIO_WritePin(DTU_RST_GPIO_Port, DTU_RST_Pin, GPIO_PIN_SET);
        osDelay(DTU_RST_HOLD_MS);
        HAL_GPIO_WritePin(DTU_RST_GPIO_Port, DTU_RST_Pin, GPIO_PIN_RESET);
        osDelay(5000);
        DTU_Init();
        dtu_disconnect_cnt = 0;
      }
    }
    else
    {
      dtu_disconnect_cnt = 0;

      if ((HAL_GetTick() - last_report) >= DTU_REPORT_INTERVAL)
      {
        DTU_SendTelemetry("0", 1);
        last_report = HAL_GetTick();
      }
    }

    osDelay(200);
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
    /* ---- KEY_MODE (PA4)：中断驱动 ---- */
    if (osSemaphoreAcquire(xSem_Key, 10) == osOK)
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
            g_work_mode = (WorkMode_t)(((int)g_work_mode + 1) % 3);
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

    /* ---- KEY_SET (PB12)：轮询边沿检测 ---- */
    uint8_t pb12_now = HAL_GPIO_ReadPin(KEY_SET_GPIO_Port, KEY_SET_Pin);
    if (pb12_prev == 1 && pb12_now == 0)
    {
        osDelay(30);
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

    osDelay(20);
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
           (unsigned)osThreadGetStackSpace(WifiTaskHandle),
           (unsigned)osThreadGetStackSpace(KeyTaskHandle),
           (unsigned)osThreadGetStackSpace(defaultTaskHandle));
  UART2_Print(line);
}

static void DTU_Init(void)
{
  HAL_GPIO_WritePin(DTU_RST_GPIO_Port, DTU_RST_Pin, GPIO_PIN_RESET);
  memset((void *)dtu_rx_buf, 0, sizeof(dtu_rx_buf));
  dtu_rx_idx = 0;
  if (HAL_UART_Receive_IT(&huart1, &dtu_rx_byte, 1) != HAL_OK)
  {
    UART2_Print("[ERR] USART1 RX arm failed\r\n");
  }

  const char *msg = "[DTU] init done\r\n";
  UART2_Print(msg);
}

static uint8_t DTU_IsConnected(void)
{
#if DTU_USE_RDY_PIN
  return (HAL_GPIO_ReadPin(DTU_RDY_GPIO_Port, DTU_RDY_Pin) == GPIO_PIN_SET) ? 1 : 0;
#else
  return 1;
#endif
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
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    if (dtu_rx_byte == '\n' || dtu_rx_byte == '\r')
    {
      if (dtu_rx_idx > 0)
      {
        dtu_rx_buf[dtu_rx_idx] = '\0';
        strncpy(dtu_cmd_buf, (char *)dtu_rx_buf, CMD_BUF_SIZE - 1);
        dtu_cmd_buf[CMD_BUF_SIZE - 1] = '\0';
        dtu_cmd_ready = 1;
        dtu_rx_idx = 0;
      }
    }
    else
    {
      if (dtu_rx_idx < DTU_RX_BUF_SIZE - 1)
      {
        dtu_rx_buf[dtu_rx_idx++] = dtu_rx_byte;
      }
      else
      {
        dtu_rx_idx = 0;
      }
    }

    HAL_UART_Receive_IT(&huart1, &dtu_rx_byte, 1);
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
