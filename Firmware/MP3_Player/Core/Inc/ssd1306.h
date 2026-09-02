#ifndef __SSD1306_H__
#define __SSD1306_H__

/* ----------------------------------------------------------------------------
 * LIBRERÍAS DE INCLUSIÓN (DEPENDENCIAS)
 * ---------------------------------------------------------------------------- */
#include "stm32f4xx_hal.h"  /* Trae las funciones del hardware de la STM32F103 (I2C, GPIO, etc.) */
#include "ssd1306_fonts.h"  /* Trae las estructuras y tablas de tipografías (Font_7x10, etc.) */

/* ----------------------------------------------------------------------------
 * CONFIGURACIÓN DE PARÁMETROS DEL HARDWARE Y BUS I2C
 * ---------------------------------------------------------------------------- */
/* Define qué puerto I2C se usará (por defecto I2C1 en pines PB6/PB7) */
#define SSD1306_I2C_PORT        hi2c1

/* Dirección I2C base del chip SSD1306 (0x3C) desplazada 1 bit a la izquierda (0x78)
 * Requier formato de 8 bits, dejando el bit 0 libre */
#define SSD1306_I2C_ADDR        (0x3C << 1)

/* ----------------------------------------------------------------------------
 * DEFINICIÓN DE TIPOS DE DATOS Y ENUMERACIONES
 * ---------------------------------------------------------------------------- */
/* Enumeración para especificar el color del píxel al dibujar */
typedef enum {
    Black = 0x00,  /* Píxel apagado / Color de fondo (negro) */
    White = 0x01   /* Píxel encendido / Color de primer plano (blanco/azul/amarillo) */
} SSD1306_COLOR;

/* ----------------------------------------------------------------------------
 * VARIABLES EXTERNAS
 * ---------------------------------------------------------------------------- */
/* Le indica al compilador que la estructura de configuración I2C está creada
 * en otro archivo (normalmente en main.c) pero le da acceso desde este módulo */
extern I2C_HandleTypeDef SSD1306_I2C_PORT;

/* ----------------------------------------------------------------------------
 * FUNCIONES PÚBLICAS (INTERFAZ DE SOFTWARE)
 * ---------------------------------------------------------------------------- */

/**
  * @brief  Inicializa la pantalla OLED SSD1306.
  * @note   Envía la secuencia de comandos de arranque (Charge Pump, contraste, modo de direccionamiento).
  * @retval uint8_t: Retorna 1 si la inicialización fue exitosa, 0 si hubo un error de I2C.
  */
uint8_t ssd1306_Init(void);

/**
  * @brief  Llena todo el buffer de la pantalla con un solo color.
  * @param  color: Color con el que se pintará toda la pantalla (Black para borrar, White para encender todo).
  * @retval Ninguno.
  */
void ssd1306_Fill(SSD1306_COLOR color);

/**
  * @brief  Transfiere todo el buffer de memoria RAM (1024 bytes) a la pantalla OLED vía I2C.
  * @note   Esta función es la que realmente actualiza la imagen visible.
  * @retval Ninguno.
  */
void ssd1306_UpdateScreen(void);

/**
  * @brief  Establece la posición actual del cursor de dibujo en la pantalla.
  * @param  x: Coordenada horizontal en píxeles (rango: 0 a 127).
  * @param  y: Coordenada vertical en píxeles (rango: 0 a 63).
  * @retval Ninguno.
  */
void ssd1306_SetCursor(uint8_t x, uint8_t y);

/**
  * @brief  Escribe una cadena de texto a partir de la posición actual del cursor.
  * @param  str: Puntero a la cadena de caracteres terminada en nulo (ejemplo: "Hola Mundo").
  * @param  Font: Estructura con la tipografía deseada (ejemplo: Font_7x10).
  * @param  color: Color con el que se dibujarán las letras (White o Black).
  * @retval char: Retorna el último carácter escrito con éxito, o 0 si no cupo en la pantalla.
  */
char ssd1306_WriteString(char* str, FontDef Font, SSD1306_COLOR color);

#endif /* __SSD1306_H__ */
