/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Reproductor de audio WAV en tiempo real para STM32F446RE.
  *                   Pipeline: FatFs (SPI1) -> DMA Ping-Pong Buffer -> I2S2 -> DAC PCM5102A.
  * @author         : polcf03
  * @note           : Implementa un buffer circular de doble mitad (Ping-Pong)
  *                   sincronizado mediante interrupciones DMA (Half/Complete Transfer).
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ff.h"
#include "string.h"
#include "stdio.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/**
 * @brief Estados principales de la máquina de control de reproducción de audio.
 */
typedef enum {
    PLAYER_STOPPED = 0, /*!< Reproducción detenida, DMA inactiva */
    PLAYER_PLAYING,     /*!< Audio en streaming continuo vía DMA */
    PLAYER_PAUSED       /*!< Reloj/transmisión de audio en pausa, archivo abierto */
} PlayerState_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/**
 * @brief Tamaño total del búfer circular en muestras de 16 bits (uint16_t).
 * @details 4096 muestras equivalen a 2048 muestras por canal (L+R estéreo).
 *          A 44.1 kHz, el búfer representa ~46.4 ms totales de audio.
 *          Cada semibúfer (2048 palabras de 16 bits / 1024 muestras estéreo) otorga un
 *          margen de ~23.2 ms a la CPU para leer de la tarjeta SD sin causar underrun.
 */
#define AUDIO_BUFFER_SIZE   4096
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;        /*!< Manejador del bus I2C1 (Pantalla OLED) */
I2S_HandleTypeDef hi2s2;        /*!< Manejador de la interfaz de audio I2S2 (DAC PCM5102A) */
DMA_HandleTypeDef hdma_spi2_tx; /*!< Manejador de DMA1 Stream 4 para TX I2S2 */
SPI_HandleTypeDef hspi1;        /*!< Manejador del bus SPI1 (Lector SD) */

/* USER CODE BEGIN PV */
/* Objetos del Middleware FatFs */
FATFS   FatFs;               /*!< Objeto de montaje del sistema de archivos en volumen lógico */
FIL     audioFile;           /*!< Descriptor del archivo de audio actualmente abierto */
FRESULT fr;                  /*!< Código de resultado de las operaciones FatFs */
UINT    bytesRead;           /*!< Contador de bytes leídos en cada ráfaga de f_read */

/* Variables de control del pipeline de audio */
uint16_t audioBuffer[AUDIO_BUFFER_SIZE]; /*!< Búfer circular compartido entre CPU y DMA */
volatile uint8_t bufferHalfFull = 0;    /*!< Flag de sincronización ISR-Main:
                                             1 = Semibúfer 0 consumido (DMA transmite mitad 1)
                                             2 = Semibúfer 1 consumido (DMA transmite mitad 0) */
uint32_t audioDataOffset = 44;          /*!< Posición en bytes donde comienzan las muestras PCM */
PlayerState_t playerState = PLAYER_STOPPED; /*!< Estado actual del reproductor */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2S2_Init(void);
static void MX_SPI1_Init(void);

/* USER CODE BEGIN PFP */
void Draw_PlayerScreen(const char* songName);
void Display_Error(const char* msg);
uint32_t WAV_GetDataOffset(FIL* fp);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief  Localiza dinámicamente el inicio de los datos PCM (chunk "data") en un archivo WAV.
 * @details Examina la cabecera del archivo en búsqueda de la etiqueta ASCII "data".
 *          Evita errores de alineación de 16 bits provocados por metadatos opcionales (etiquetas LIST/INFO).
 * @param  fp: Puntero al descriptor de archivo FatFs ya abierto.
 * @retval Desplazamiento en bytes (offset) desde el inicio del archivo donde inician las muestras PCM.
 */
uint32_t WAV_GetDataOffset(FIL* fp) {
    uint8_t buffer[512];
    UINT br;

    f_lseek(fp, 0);
    if (f_read(fp, buffer, sizeof(buffer), &br) != FR_OK || br < 44) {
        return 44; /* Retorno seguro por defecto si el archivo es un WAV canónico de 44 bytes */
    }

    /* Búsqueda de la firma "data" en los primeros 512 bytes de la cabecera */
    for (uint32_t i = 0; i < br - 4; i++) {
        if (buffer[i] == 'd' && buffer[i+1] == 'a' &&
            buffer[i+2] == 't' && buffer[i+3] == 'a') {
            return i + 8; /* Saltar el identificador "data" (4 bytes) + campo ChunkSize (4 bytes) */
        }
    }
    return 44;
}

/**
 * @brief  Renderiza la interfaz visual del reproductor en la pantalla OLED SSD1306 vía I2C.
 * @param  songName: Cadena de caracteres con el título del tema a mostrar.
 * @retval Ninguno
 */
void Draw_PlayerScreen(const char* songName) {
    ssd1306_Fill(Black);

    /* Encabezado */
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString("STM32 PLAYER", Font_7x10, White);

    /* Nombre del tema */
    ssd1306_SetCursor(0, 18);
    ssd1306_WriteString((char*)songName, Font_7x10, White);

    /* Estado del reproductor */
    ssd1306_SetCursor(0, 34);
    ssd1306_WriteString("Status: PLAY", Font_7x10, White);

    /* Indicador de progreso estático */
    ssd1306_SetCursor(0, 50);
    ssd1306_WriteString("[=====>    ]", Font_7x10, White);

    ssd1306_UpdateScreen();
}

/**
 * @brief  Detiene la ejecución del sistema y muestra un mensaje de fallo crítico en la pantalla.
 * @param  msg: Cadena de texto descriptiva del error detectado.
 * @retval Ninguno (Entra en un bucle de bloqueo permanente).
 */
void Display_Error(const char* msg) {
    ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString("FATAL ERROR", Font_7x10, White);
    ssd1306_SetCursor(0, 20);
    ssd1306_WriteString((char*)msg, Font_7x10, White);
    ssd1306_UpdateScreen();
    while (1);
}

/* USER CODE END 0 */

/**
  * @brief  Punto de entrada principal de la aplicación.
  * @retval int
  */
int main(void)
{
  /* Reset de todos los periféricos, inicialización de la interfaz Flash y SysTick */
  HAL_Init();

  /* Configuración del reloj del sistema (System Clock) */
  SystemClock_Config();

  /* Inicialización de periféricos generados por STM32CubeMX */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_I2S2_Init();
  MX_FATFS_Init();
  MX_SPI1_Init();

  /* USER CODE BEGIN 2 */

  /* 1. Inicialización de la pantalla OLED y preparación del bus SPI de la SD */
  ssd1306_Init();
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET); /* Deseleccionar Chip Select (CS) de la SD (HIGH) */
  HAL_Delay(50);

  /* 2. Montaje del sistema de archivos FatFs a baja velocidad (<400 kHz requerido para inicializar la tarjeta SD) */
  fr = f_mount(&FatFs, "", 1);
  if (fr != FR_OK) {
      Display_Error("SD MOUNT FAIL");
  }

  /* 3. Reconfiguración de velocidad del bus SPI a alta velocidad (10.5 MHz) para transferencia de audio */
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  HAL_SPI_Init(&hspi1);

  /* 4. Apertura del archivo WAV desde la SD */
  fr = f_open(&audioFile, "test.wav", FA_READ);
  if (fr != FR_OK) {
      Display_Error("FILE NOT FOUND");
  }

  /* 5. Análisis de cabecera para ignorar metadatos y precarga del búfer Ping-Pong completo */
  audioDataOffset = WAV_GetDataOffset(&audioFile);
  f_lseek(&audioFile, audioDataOffset);

  fr = f_read(&audioFile, audioBuffer, sizeof(audioBuffer), &bytesRead);
  if (fr != FR_OK || bytesRead == 0) {
      Display_Error("FILE READ ERR");
  }

  /* 6. Actualización de la pantalla e inicio del streaming I2S con DMA en modo circular */
  Draw_PlayerScreen("Your Lie In April!!");
  playerState = PLAYER_PLAYING;
  HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t*)audioBuffer, AUDIO_BUFFER_SIZE);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
      /**
       * MANTENIMIENTO DEL BÚFER CIRCULAR PING-PONG:
       *
       * Evento 1: La DMA ha completado el envío de la primera mitad del búfer (Muestras 0 a 2047).
       * Acción CPU: Recarga la primera mitad con datos nuevos desde la SD mientras la DMA
       *             sigue transmitiendo la segunda mitad de forma ininterrumpida.
       */
      if (bufferHalfFull == 1) {
          bufferHalfFull = 0;
          fr = f_read(&audioFile, &audioBuffer[0], sizeof(audioBuffer) / 2, &bytesRead);
          if (bytesRead < (sizeof(audioBuffer) / 2) || fr != FR_OK) {
              f_lseek(&audioFile, audioDataOffset); /* Rebobinar al inicio del audio si alcanza el EOF */
          }
      }

      /**
       * Evento 2: La DMA ha completado el envío de la segunda mitad del búfer (Muestras 2048 a 4095).
       * Acción CPU: Recarga la segunda mitad con datos nuevos desde la SD mientras la DMA
       *             vuelve a transmitir la primera mitad en bucle.
       */
      if (bufferHalfFull == 2) {
          bufferHalfFull = 0;
          fr = f_read(&audioFile, &audioBuffer[AUDIO_BUFFER_SIZE / 2], sizeof(audioBuffer) / 2, &bytesRead);
          if (bytesRead < (sizeof(audioBuffer) / 2) || fr != FR_OK) {
              f_lseek(&audioFile, audioDataOffset); /* Rebobinar al inicio del audio si alcanza el EOF */
          }
      }
  }
  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */
  /* USER CODE END 3 */
}

/**
  * @brief Configuración del sistema de relojes (System Clock).
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Inicialización del periférico I2C1 (Pantalla OLED).
  * @retval None
  */
static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Inicialización del periférico I2S2 (Interfaz de Audio).
  * @retval None
  */
static void MX_I2S2_Init(void)
{
  __HAL_RCC_SPI2_CLK_ENABLE();
  hi2s2.Instance = SPI2;
  hi2s2.Init.Mode = I2S_MODE_MASTER_TX;
  hi2s2.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s2.Init.DataFormat = I2S_DATAFORMAT_16B_EXTENDED;
  hi2s2.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
  hi2s2.Init.AudioFreq = I2S_AUDIOFREQ_44K;
  hi2s2.Init.CPOL = I2S_CPOL_LOW;
  hi2s2.Init.ClockSource = I2S_CLOCK_PLL;
  hi2s2.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
  if (HAL_I2S_Init(&hi2s2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Inicialización del periférico SPI1 (Comunicación con SD).
  * @retval None
  */
static void MX_SPI1_Init(void)
{
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Inicialización de la controladora DMA para I2S2.
  * @retval None
  */
static void MX_DMA_Init(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();
  HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
}

/**
  * @brief Inicialización general de los pines GPIO.
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* Habilitación de relojes de los puertos GPIO */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* Nivel lógico inicial del pin de selección SD (CS) */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);

  /* Configuración de pin: Botón azul B1 (PC13) */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /* Configuración de pines: Botones de control NEXT (PA0) y PREV (PA1) */
  GPIO_InitStruct.Pin = BTN_NEXT_Pin|BTN_PREV_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* Configuración de pines: UART2 TX/RX (PA2 / PA3) */
  GPIO_InitStruct.Pin = USART_TX_Pin|USART_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* Configuración del pin: Chip Select (CS) para tarjeta SD (PB6) */
  GPIO_InitStruct.Pin = GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */

/**
 * @brief  Callback de interrupción por transferencia a mitad de búfer DMA I2S (Half Transfer).
 * @details Invocado por HAL cuando la DMA transmite el primer bloque `[0 ... (AUDIO_BUFFER_SIZE/2) - 1]`.
 * @param  hi2s: Puntero a la estructura de configuración I2S.
 * @retval None
 */
void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
    if (hi2s->Instance == SPI2) {
        bufferHalfFull = 1; /* Señaliza al bucle principal para recargar la mitad 0 */
    }
}

/**
 * @brief  Callback de interrupción por transferencia completa de búfer DMA I2S (Transfer Complete).
 * @details Invocado por HAL cuando la DMA transmite el segundo bloque `[(AUDIO_BUFFER_SIZE/2) ... AUDIO_BUFFER_SIZE - 1]`.
 * @param  hi2s: Puntero a la estructura de configuración I2S.
 * @retval None
 */
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s) {
    if (hi2s->Instance == SPI2) {
        bufferHalfFull = 2; /* Señaliza al bucle principal para recargar la mitad 1 */
    }
}

/* USER CODE END 4 */

/**
  * @brief  Manejador global de errores irrecuperables.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
