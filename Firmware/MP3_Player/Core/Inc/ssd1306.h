/**
  ******************************************************************************
  * @file    ssd1306.h
  * @author  polcf03
  * @brief   Driver y primitivas gráficas para display OLED SSD1306 (128x64)
  *          comunicado por bus I2C en microcontroladores STM32F4.
  ******************************************************************************
  */

#ifndef __SSD1306_H__
#define __SSD1306_H__

/* ----------------------------------------------------------------------------
 * DEPENDENCIAS
 * ---------------------------------------------------------------------------- */
#include "stm32f4xx_hal.h"  /* Acceso a periféricos HAL y registros del STM32F4 */
#include "ssd1306_fonts.h"  /* Definiciones de tipografías y tablas de glifos */

/* ----------------------------------------------------------------------------
 * PARÁMETROS DE HARDWARE Y BUS I2C
 * ---------------------------------------------------------------------------- */
/* Dimensiones físicas del panel OLED en píxeles */
#define SSD1306_WIDTH           128
#define SSD1306_HEIGHT          64

/* Periférico I2C asignado (I2C1: PB8=SCL, PB9=SDA en NUCLEO-F446RE) */
#define SSD1306_I2C_PORT        hi2c1

/* Dirección I2C base del chip (0x3C de 7 bits) desplazada 1 bit a la izquierda
 * para cumplir el formato de 8 bits exigido por la HAL de ST (0x78) */
#define SSD1306_I2C_ADDR        (0x3C << 1)

/* ----------------------------------------------------------------------------
 * TIPOS DE DATOS Y ENUMERACIONES
 * ---------------------------------------------------------------------------- */
/**
 * @brief Estado de color para la manipulación de píxeles en el Framebuffer.
 */
typedef enum {
    Black = 0x00,  /*!< Píxel apagado (Fondo negro) */
    White = 0x01   /*!< Píxel encendido (Iluminado) */
} SSD1306_COLOR;

/* ----------------------------------------------------------------------------
 * REFERENCIAS EXTERNAS
 * ---------------------------------------------------------------------------- */
extern I2C_HandleTypeDef SSD1306_I2C_PORT;

/* ----------------------------------------------------------------------------
 * FUNCIONES DE CONTROL Y GESTIÓN DE PANTALLA
 * ---------------------------------------------------------------------------- */

/**
 * @brief  Inicializa el controlador SSD1306 con la secuencia recomendada por el fabricante.
 * @note   Configura el multiplicador de voltaje (Charge Pump), contraste y direccionamiento.
 * @retval 1 si la inicialización I2C fue exitosa, 0 en caso de fallo.
 */
uint8_t ssd1306_Init(void);

/**
 * @brief  Rellena todo el buffer de vídeo con un único color.
 * @param  color: Color con el que se llenará el buffer (Black para borrar, White para encender todo).
 * @retval Ninguno.
 */
void ssd1306_Fill(SSD1306_COLOR color);

/**
 * @brief  Transfiere el contenido completo del Framebuffer en RAM (1024 bytes) a la pantalla por I2C.
 * @note   Esta función es la que hace visibles los cambios en el cristal.
 * @retval Ninguno.
 */
void ssd1306_UpdateScreen(void);

/* ----------------------------------------------------------------------------
 * PRIMITIVAS GRÁFICAS (DIBUJO 2D)
 * ---------------------------------------------------------------------------- */

/**
 * @brief  Modifica el estado de un único píxel dentro del Framebuffer.
 * @param  x: Coordenada horizontal (0 a SSD1306_WIDTH - 1).
 * @param  y: Coordenada vertical (0 a SSD1306_HEIGHT - 1).
 * @param  color: Estado del píxel (White o Black).
 * @retval Ninguno.
 */
void ssd1306_DrawPixel(uint8_t x, uint8_t y, SSD1306_COLOR color);

/**
 * @brief  Dibuja una línea recta entre dos coordenadas usando el algoritmo de Bresenham.
 * @param  x0, y0: Coordenada inicial.
 * @param  x1, y1: Coordenada final.
 * @param  color: Color de la línea (White o Black).
 * @retval Ninguno.
 */
void ssd1306_DrawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, SSD1306_COLOR color);

/**
 * @brief  Dibuja el contorno de un rectángulo.
 * @param  x: Coordenada horizontal de la esquina superior izquierda.
 * @param  y: Coordenada vertical de la esquina superior izquierda.
 * @param  w: Ancho del rectángulo en píxeles.
 * @param  h: Alto del rectángulo en píxeles.
 * @param  color: Color del contorno (White o Black).
 * @retval Ninguno.
 */
void ssd1306_DrawRectangle(uint8_t x, uint8_t y, uint8_t w, uint8_t h, SSD1306_COLOR color);

/**
 * @brief  Dibuja un rectángulo sólido relleno.
 * @note   Ideal para barras de volumen dinámicas y barras de progreso.
 * @param  x, y: Esquina superior izquierda.
 * @param  w, h: Dimensiones (ancho y alto).
 * @param  color: Color del relleno.
 * @retval Ninguno.
 */
void ssd1306_DrawFilledRectangle(uint8_t x, uint8_t y, uint8_t w, uint8_t h, SSD1306_COLOR color);

/* ----------------------------------------------------------------------------
 * FUNCIONES DE TEXTO Y FUENTES
 * ---------------------------------------------------------------------------- */

/**
 * @brief  Posiciona el cursor para las operaciones de escritura de texto.
 * @param  x: Columna de inicio (0 a SSD1306_WIDTH - 1).
 * @param  y: Fila de inicio (0 a SSD1306_HEIGHT - 1).
 * @retval Ninguno.
 */
void ssd1306_SetCursor(uint8_t x, uint8_t y);

/**
 * @brief  Escribe un único carácter en la posición actual del cursor.
 * @param  ch: Carácter ASCII a renderizar (ej. 'A').
 * @param  Font: Definición de tipografía a usar (ej. Font_7x10).
 * @param  color: Color de los trazos (White o Black).
 * @retval Carácter escrito con éxito, o 0 si no cupo en los límites del panel.
 */
char ssd1306_WriteChar(char ch, FontDef Font, SSD1306_COLOR color);

/**
 * @brief  Escribe una cadena de texto a partir de la posición actual del cursor.
 * @param  str: Puntero a cadena terminada en nulo ('\0').
 * @param  Font: Tipografía deseada.
 * @param  color: Color del texto.
 * @retval Último carácter procesado correctamente.
 */
char ssd1306_WriteString(char* str, FontDef Font, SSD1306_COLOR color);

#endif /* __SSD1306_H__ */
