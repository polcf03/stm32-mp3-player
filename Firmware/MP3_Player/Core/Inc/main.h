/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Encabezado principal de la aplicación. Contiene las
  *                   inclusiones globales, prototipos de funciones públicas y
  *                   los alias de hardware (GPIOs) definidos para el proyecto.
  * @author         : Tu Nombre / Proyecto Reproductor WAV STM32
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * Todos los derechos reservados.
  *
  * Este software está licenciado bajo los términos que se encuentran en el
  * archivo LICENSE en el directorio raíz de este componente de software.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define para evitar inclusión recursiva ------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"  /*!< Librería de abstracción de hardware HAL de STM32F4 */

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdbool.h>
#include "ff.h"             /*!< Librería FatFs para el sistema de archivos de la SD */
#include "ssd1306.h"        /*!< Controlador para el display OLED I2C */
#include "ssd1306_fonts.h"  /*!< Fuentes tipográficas para la pantalla */
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

/**
  * @brief  Manejador de errores globales del sistema.
  * @details Esta función se invoca cuando ocurre un fallo crítico no recuperable
  *          durante la inicialización de periféricos o ejecución del sistema.
  *          Bloquea la ejecución en un bucle infinito para prevenir comportamientos anómalos.
  * @retval None
  */
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/

/**
  * @defgroup Mapeo_de_Pines_GPIO Asignación de Alias de Hardware
  * @brief Definicíón de etiquetas para los pines de E/S del microcontrolador.
  * @{
  */

/* Botón de usuario onboard (Azul) */
#define B1_Pin GPIO_PIN_13
#define B1_GPIO_Port GPIOC

/* Controles del Reproductor de Audio (Botones) */
#define BTN_NEXT_Pin GPIO_PIN_0        /*!< Pin de entrada para avanzar de canción */
#define BTN_NEXT_GPIO_Port GPIOA
#define BTN_PREV_Pin GPIO_PIN_1        /*!< Pin de entrada para retroceder de canción */
#define BTN_PREV_GPIO_Port GPIOA

/* Interfaz de Comunicación Serial USART2 (ST-LINK Virtual COM Port) */
#define USART_TX_Pin GPIO_PIN_2        /*!< Transmisión UART hacia la PC */
#define USART_TX_GPIO_Port GPIOA
#define USART_RX_Pin GPIO_PIN_3        /*!< Recepción UART desde la PC */
#define USART_RX_GPIO_Port GPIOA

/* Pines de Depuración SWD (Serial Wire Debug) */
#define TMS_Pin GPIO_PIN_13            /*!< Línea SWDIO para programación/depuración */
#define TMS_GPIO_Port GPIOA
#define TCK_Pin GPIO_PIN_14            /*!< Reloj SWCLK para depuración */
#define TCK_GPIO_Port GPIOA

/**
  * @}
  */

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
