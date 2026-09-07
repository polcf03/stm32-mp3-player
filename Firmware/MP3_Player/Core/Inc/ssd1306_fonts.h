/**
  ******************************************************************************
  * @file    ssd1306_fonts.h
  * @author  polcf03
  * @brief   Definición de estructuras y exportación de fuentes tipográficas
  *          para el controlador OLED SSD1306.
  ******************************************************************************
  */

#ifndef __SSD1306_FONTS_H__
#define __SSD1306_FONTS_H__

#include <stdint.h>

/* ----------------------------------------------------------------------------
 * ESTRUCTURAS DE DATOS
 * ---------------------------------------------------------------------------- */

/**
 * @brief  Estructura descriptor de una fuente tipográfica de mapa de bits.
 * @note   El uso de 'const' asegura que los datos residan permanentemente
 *         en la memoria Flash (ROM) del STM32, ahorrando memoria RAM.
 */
typedef struct {
    const uint8_t  FontWidth;    /*!< Ancho de cada glifo en píxeles */
    const uint8_t  FontHeight;   /*!< Alto de cada glifo en píxeles */
    const uint16_t *data;        /*!< Puntero a la tabla de mapas de bits en Flash.
                                      Los caracteres están ordenados según la tabla
                                      ASCII estándar desde el espacio ' ' (32) al '~' (126). */
} FontDef;

/* ----------------------------------------------------------------------------
 * DECLARACIÓN DE FUENTES DISPONIBLES (EXTERN)
 * ---------------------------------------------------------------------------- */

/**
 * @brief  Fuente de tamaño medio: 7 píxeles de ancho x 10 píxeles de alto.
 * @note   Es la tipografía principal del reproductor: equilibrada y legible
 *         para títulos de canciones, estados y etiquetas de menú.
 */
extern FontDef Font_7x10;

#endif /* __SSD1306_FONTS_H__ */
