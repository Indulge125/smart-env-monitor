/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define KEY_MODE_Pin GPIO_PIN_4
#define KEY_MODE_GPIO_Port GPIOA
#define KEY_MODE_EXTI_IRQn EXTI4_IRQn
#define BUZZER_Pin GPIO_PIN_6
#define BUZZER_GPIO_Port GPIOA
#define LED_STATUS_Pin GPIO_PIN_7
#define LED_STATUS_GPIO_Port GPIOA
#define DTU_RDY_Pin GPIO_PIN_10
#define DTU_RDY_GPIO_Port GPIOB
#define DTU_RST_Pin GPIO_PIN_11
#define DTU_RST_GPIO_Port GPIOB
#define KEY_SET_Pin GPIO_PIN_12
#define KEY_SET_GPIO_Port GPIOB
#define LED_ALARM_Pin GPIO_PIN_11
#define LED_ALARM_GPIO_Port GPIOA
#define OLED_SCK_Pin GPIO_PIN_8
#define OLED_SCK_GPIO_Port GPIOB
#define OLED_SDA_Pin GPIO_PIN_9
#define OLED_SDA_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
#define FW_VERSION "V0.1"
#define HW_VERSION "V1.0"
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
