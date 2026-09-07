/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Reproductor de audio WAV en tiempo real para STM32F446RE.
  *                   Pipeline: FatFs (SPI1) -> DMA Ping-Pong Buffer -> I2S2 -> DAC PCM5102A.
  * @author         : polcf03
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
 * @brief Estados de la máquina de control de reproducción de audio.
 */
typedef enum {
    PLAYER_STOPPED = 0, /* Reproducción detenida, DMA inactiva */
    PLAYER_PLAYING,     /* Audio en streaming continuo por DMA */
    PLAYER_PAUSED       /* Reloj de audio pausado, archivo abierto */
} PlayerState_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/**
 * @brief Tamaño del búfer circular en muestras (uint16_t).
 *        4096 muestras = 2048 muestras estéreo (L+R).
 *        A 44.1 kHz, el búfer total representa ~46.4 ms de audio.
 *        Cada semibúfer (Ping-Pong) otorga un margen de ~23.2 ms a la CPU para leer de la SD.
 */
#define AUDIO_BUFFER_SIZE   4096
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
I2S_HandleTypeDef hi2s2;
DMA_HandleTypeDef hdma_spi2_tx;
SPI_HandleTypeDef hspi1;

/* USER CODE BEGIN PV */
/* Objetos del Middleware FatFs */
FATFS   FatFs;               /* Objeto de montaje del sistema de archivos en volumen lógico */
FIL     audioFile;           /* Descriptor del archivo de audio actualmente abierto */
FRESULT fr;                  /* Código de resultado de las operaciones FatFs */
UINT    bytesRead;           /* Contador de bytes leídos en cada ráfaga de f_read */

/* Variables de control del pipeline de audio */
uint16_t audioBuffer[AUDIO_BUFFER_SIZE]; /* Búfer circular compartido entre CPU y DMA */
volatile uint8_t bufferHalfFull = 0;    /* Flag de sincronización ISR-Main:
                                            1 = Semibúfer 0 consumido (DMA transmite mitad 1)
                                            2 = Semibúfer 1 consumido (DMA transmite mitad 0) */
uint32_t audioDataOffset = 44;          /* Posición en bytes donde comienzan las muestras PCM */
PlayerState_t playerState = PLAYER_STOPPED;
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
 * @note   Evita errores de alineación de 16 bits provocados por metadatos (etiquetas LIST/INFO).
 * @param  fp: Puntero al descriptor de archivo FatFs ya abierto.
 * @retval Desplazamiento en bytes desde el inicio del archivo donde inician las muestras PCM.
 */
uint32_t WAV_GetDataOffset(FIL* fp) {
    uint8_t buffer[512];
    UINT br;

    f_lseek(fp, 0);
    if (f_read(fp, buffer, sizeof(buffer), &br) != FR_OK || br < 44) {
        return 44; /* Retorno seguro por defecto si el archivo es un WAV canónico */
    }

    /* Búsqueda de la firma "data" en los primeros 512 bytes de cabecera */
    for (uint32_t i = 0; i < br - 4; i++) {
        if (buffer[i] == 'd' && buffer[i+1] == 'a' &&
            buffer[i+2] == 't' && buffer[i+3] == 'a') {
            return i + 8; /* Saltar el identificador de 4 bytes + campo ChunkSize de 4 bytes */
        }
    }
    return 44;
}

/**
 * @brief  Renderiza la interfaz de usuario en el display OLED SSD1306 vía I2C.
 * @param  songName: Cadena de texto con el nombre de la pista a mostrar.
 * @retval Ninguno.
 */
void Draw_PlayerScreen(const char* songName) {
    ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString("STM32 PLAYER", Font_7x10, White);

    ssd1306_SetCursor(0, 18);
    ssd1306_WriteString((char*)songName, Font_7x10, White);

    ssd1306_SetCursor(0, 34);
    ssd1306_WriteString("Status: PLAY", Font_7x10, White);

    ssd1306_SetCursor(0, 50);
    ssd1306_WriteString("[=====>    ]", Font_7x10, White);

    ssd1306_UpdateScreen();
}

/**
 * @brief  Detiene la ejecución y muestra el mensaje de fallo en la pantalla OLED.
 * @param  msg: Descripción breve del error ocurrido.
 * @retval Ninguno (bucle infinito).
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

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  /* Inicialización de controladores periféricos */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_I2S2_Init();
  MX_FATFS_Init();
  MX_SPI1_Init();

  /* USER CODE BEGIN 2 */
  /* 1. Inicialización de periféricos y estado del bus */
  ssd1306_Init();
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET); /* Deseleccionar SD (CS en HIGH) */
  HAL_Delay(50);

  /* 2. Montaje del sistema de archivos en baja velocidad (<400 kHz exigido por protocolo SD) */
  fr = f_mount(&FatFs, "", 1);
  if (fr != FR_OK) {
      Display_Error("SD MOUNT FAIL");
  }

  /* 3. Transición a alta velocidad (10.5 MHz) para garantizar ancho de banda de audio */
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  HAL_SPI_Init(&hspi1);

  /* 4. Apertura del archivo de audio */
  fr = f_open(&audioFile, "test.wav", FA_READ);
  if (fr != FR_OK) {
      Display_Error("FILE NOT FOUND");
  }

  /* 5. Análisis de cabecera y precarga del primer bloque completo */
  audioDataOffset = WAV_GetDataOffset(&audioFile);
  f_lseek(&audioFile, audioDataOffset);

  fr = f_read(&audioFile, audioBuffer, sizeof(audioBuffer), &bytesRead);
  if (fr != FR_OK || bytesRead == 0) {
      Display_Error("FILE READ ERR");
  }

  /* 6. Inicialización visual e inicio de la transmisión DMA en modo Circular */
  Draw_PlayerScreen("test.wav");
  playerState = PLAYER_PLAYING;
  HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t*)audioBuffer, AUDIO_BUFFER_SIZE);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
      /**
       * Arquitectura Ping-Pong Buffering:
       * Cuando la DMA termina la primera mitad del búfer, la CPU lee el siguiente
       * bloque de la SD y lo escribe en la mitad 0 mientras la DMA transmite la mitad 1.
       */
      if (bufferHalfFull == 1) {
          bufferHalfFull = 0;
          fr = f_read(&audioFile, &audioBuffer[0], sizeof(audioBuffer) / 2, &bytesRead);
          if (bytesRead < (sizeof(audioBuffer) / 2) || fr != FR_OK) {
              f_lseek(&audioFile, audioDataOffset); /* Rebobinar al inicio si alcanza el EOF */
          }
      }

      /**
       * Cuando la DMA termina la segunda mitad del búfer, la CPU recarga
       * la mitad 1 mientras la DMA transmite la mitad 0.
       */
      if (bufferHalfFull == 2) {
          bufferHalfFull = 0;
          fr = f_read(&audioFile, &audioBuffer[AUDIO_BUFFER_SIZE / 2], sizeof(audioBuffer) / 2, &bytesRead);
          if (bytesRead < (sizeof(audioBuffer) / 2) || fr != FR_OK) {
              f_lseek(&audioFile, audioDataOffset); /* Rebobinar al inicio si alcanza el EOF */
          }
      }
  }
  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
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
  * @brief I2C1 Initialization Function
  * @param None
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
  * @brief I2S2 Initialization Function
  * @param None
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
  * @brief SPI1 Initialization Function
  * @param None
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
  * @brief DMA Initialization Function
  */
static void MX_DMA_Init(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();
  HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
}

/**
  * @brief GPIO Initialization Function
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);

  /* Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /* Configure GPIO pins : BTN_NEXT_Pin BTN_PREV_Pin */
  GPIO_InitStruct.Pin = BTN_NEXT_Pin|BTN_PREV_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* Configure GPIO pins : USART_TX_Pin USART_RX_Pin */
  GPIO_InitStruct.Pin = USART_TX_Pin|USART_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* Configure GPIO pin : PB6 (CS de la SD) */
  GPIO_InitStruct.Pin = GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */
/**
 * @brief  Callback de transferencia media de la DMA I2S.
 *         Se dispara cuando la DMA ha enviado la primera mitad del búfer.
 */
void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
    if (hi2s->Instance == SPI2) {
        bufferHalfFull = 1;
    }
}

/**
 * @brief  Callback de transferencia completa de la DMA I2S.
 *         Se dispara cuando la DMA ha enviado la segunda mitad del búfer.
 */
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s) {
    if (hi2s->Instance == SPI2) {
        bufferHalfFull = 2;
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
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
