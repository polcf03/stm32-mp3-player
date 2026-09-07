/**
  ******************************************************************************
  * @file    ssd1306.c
  * @author  polcf03
  * @brief   Implementación del driver y motor gráfico 2D para SSD1306 vía I2C.
  ******************************************************************************
  */

#include "ssd1306.h"

/* ----------------------------------------------------------------------------
 * VARIABLES PRIVADAS Y MEMORIA RAM (FRAMEBUFFER)
 * ---------------------------------------------------------------------------- */

/* Framebuffer: Matriz en RAM del microcontrolador que almacena la imagen completa.
 * Tamaño: (128 píxeles de ancho * 64 píxeles de alto) / 8 bits = 1024 Bytes (1 KB). */
static uint8_t SSD1306_Buffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8];

/* Estructura para el seguimiento de la posición del cursor de texto */
typedef struct {
    uint16_t CurrentX;
    uint16_t CurrentY;
    uint8_t  Initialized;
} SSD1306_t;

static SSD1306_t SSD1306;

/* ----------------------------------------------------------------------------
 * FUNCIONES PRIVADAS DE COMUNICACIÓN I2C
 * ---------------------------------------------------------------------------- */

/**
 * @brief  Envía un byte de comando de configuración al controlador SSD1306.
 * @param  command: Código de comando (ej. 0xAF para encender el panel).
 */
static void ssd1306_WriteCommand(uint8_t command) {
    /* El byte de control 0x00 le indica al SSD1306 que el byte entrante es un comando */
    HAL_I2C_Mem_Write(&SSD1306_I2C_PORT, SSD1306_I2C_ADDR, 0x00, 1, &command, 1, 100);
}

/* ----------------------------------------------------------------------------
 * FUNCIONES PÚBLICAS DE GESTIÓN Y CONTROL
 * ---------------------------------------------------------------------------- */

/**
 * @brief  Inicializa el display según la secuencia oficial de arranque de Solomon Systech.
 * @retval 1 al completar con éxito.
 */
uint8_t ssd1306_Init(void) {
    /* Pausa para estabilizar la alimentación del panel */
    HAL_Delay(100);

    /* Secuencia de comandos de inicialización */
    ssd1306_WriteCommand(0xAE); // Display OFF (Apagar panel durante configuración)
    ssd1306_WriteCommand(0x20); // Set Memory Addressing Mode
    ssd1306_WriteCommand(0x10); // 0x10 = Page Addressing Mode
    ssd1306_WriteCommand(0xB0); // Set Page Start Address para GDDRAM en 0
    ssd1306_WriteCommand(0xC8); // COM Output Scan Direction (Remapeado vertical)
    ssd1306_WriteCommand(0x00); // Set Low Column Address
    ssd1306_WriteCommand(0x10); // Set High Column Address
    ssd1306_WriteCommand(0x40); // Set Start Line Address a 0
    ssd1306_WriteCommand(0x81); // Set Contrast Control
    ssd1306_WriteCommand(0xFF); // Contraste máximo (0x00 a 0xFF)
    ssd1306_WriteCommand(0xA1); // Set Segment Re-map (Remapeado horizontal)
    ssd1306_WriteCommand(0xA6); // Normal Display (1 = Píxel encendido, 0 = Píxel apagado)
    ssd1306_WriteCommand(0xA8); // Set Multiplex Ratio
    ssd1306_WriteCommand(0x3F); // 1/64 duty (para pantalla de 64 píxeles de alto)
    ssd1306_WriteCommand(0xA4); // Output follows RAM (Seguir contenido de memoria)
    ssd1306_WriteCommand(0xD3); // Set Display Offset
    ssd1306_WriteCommand(0x00); // Sin desplazamiento
    ssd1306_WriteCommand(0xD5); // Set Display Clock Divide Ratio / Oscillator Frequency
    ssd1306_WriteCommand(0xF0); // Frecuencia máxima recomendada
    ssd1306_WriteCommand(0xD9); // Set Pre-charge Period
    ssd1306_WriteCommand(0x22); // Periodo estándar recomendado
    ssd1306_WriteCommand(0xDA); // Set COM Pins Hardware Configuration
    ssd1306_WriteCommand(0x12); // Configuración alternativa para paneles de 128x64
    ssd1306_WriteCommand(0xDB); // Set VCOMH Deselect Level
    ssd1306_WriteCommand(0x20); // ~0.77 x Vcc
    ssd1306_WriteCommand(0x8D); // Charge Pump Setting
    ssd1306_WriteCommand(0x14); // Enable Charge Pump (Eleva 3.3V a los 7-9V necesarios)
    ssd1306_WriteCommand(0xAF); // Display ON (Encender panel)

    /* Limpiar memoria interna y reiniciar cursor */
    ssd1306_Fill(Black);
    SSD1306.CurrentX = 0;
    SSD1306.CurrentY = 0;
    SSD1306.Initialized = 1;

    return 1;
}

/**
 * @brief  Pinta todo el Framebuffer con un único color.
 */
void ssd1306_Fill(SSD1306_COLOR color) {
    uint8_t fill_val = (color == Black) ? 0x00 : 0xFF;
    for (uint32_t i = 0; i < sizeof(SSD1306_Buffer); i++) {
        SSD1306_Buffer[i] = fill_val;
    }
}

/**
 * @brief  Envía los 1024 bytes del Framebuffer en RAM a la pantalla física.
 * @note   La pantalla está dividida en 8 páginas horizontales de 8 píxeles de alto cada una.
 */
void ssd1306_UpdateScreen(void) {
    for (uint8_t page = 0; page < 8; page++) {
        ssd1306_WriteCommand(0xB0 + page); // Fijar página actual (0 a 7)
        ssd1306_WriteCommand(0x00);        // Resetear columna (parte baja)
        ssd1306_WriteCommand(0x10);        // Resetear columna (parte alta)

        /* El byte de control 0x40 indica que el bloque de 128 bytes son datos de vídeo */
        HAL_I2C_Mem_Write(&SSD1306_I2C_PORT, SSD1306_I2C_ADDR, 0x40, 1,
                          &SSD1306_Buffer[SSD1306_WIDTH * page], SSD1306_WIDTH, 100);
    }
}

/* ----------------------------------------------------------------------------
 * PRIMITIVAS GRÁFICAS (DIBUJO 2D)
 * ---------------------------------------------------------------------------- */

/**
 * @brief  Modifica un único píxel en las coordenadas (x, y) de la RAM.
 */
void ssd1306_DrawPixel(uint8_t x, uint8_t y, SSD1306_COLOR color) {
    /* Protección contra desbordamiento de memoria */
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) {
        return;
    }

    /* Cálculo de posición:
     * (y / 8) * 128 + x : Ubica el byte exacto dentro del Framebuffer.
     * (1 << (y % 8))    : Selecciona el bit vertical dentro de ese byte. */
    if (color == White) {
        SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] |= (1 << (y % 8));
    } else {
        SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] &= ~(1 << (y % 8));
    }
}

/**
 * @brief  Dibuja una línea recta entre dos puntos usando el Algoritmo de Bresenham.
 */
void ssd1306_DrawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, SSD1306_COLOR color) {
    int16_t dx = (x0 < x1) ? (x1 - x0) : (x0 - x1);
    int16_t dy = (y0 < y1) ? (y1 - y0) : (y0 - y1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;

    while (1) {
        ssd1306_DrawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/**
 * @brief  Dibuja el contorno de un rectángulo.
 */
void ssd1306_DrawRectangle(uint8_t x, uint8_t y, uint8_t w, uint8_t h, SSD1306_COLOR color) {
    if (w == 0 || h == 0) return;
    ssd1306_DrawLine(x, y, x + w - 1, y, color);
    ssd1306_DrawLine(x, y + h - 1, x + w - 1, y + h - 1, color);
    ssd1306_DrawLine(x, y, x, y + h - 1, color);
    ssd1306_DrawLine(x + w - 1, y, x + w - 1, y + h - 1, color);
}

/**
 * @brief  Dibuja un rectángulo sólido relleno.
 */
void ssd1306_DrawFilledRectangle(uint8_t x, uint8_t y, uint8_t w, uint8_t h, SSD1306_COLOR color) {
    for (uint8_t i = 0; i < h; i++) {
        ssd1306_DrawLine(x, y + i, x + w - 1, y + i, color);
    }
}

/* ----------------------------------------------------------------------------
 * FUNCIONES DE TEXTO
 * ---------------------------------------------------------------------------- */

/**
 * @brief  Establece la coordenada actual donde se comenzará a escribir texto.
 */
void ssd1306_SetCursor(uint8_t x, uint8_t y) {
    SSD1306.CurrentX = x;
    SSD1306.CurrentY = y;
}

/**
 * @brief  Dibuja un carácter en la posición actual del cursor.
 */
char ssd1306_WriteChar(char ch, FontDef Font, SSD1306_COLOR color) {
    /* Comprobar si el carácter cabe en el panel */
    if (SSD1306_WIDTH < (SSD1306.CurrentX + Font.FontWidth) ||
        SSD1306_HEIGHT < (SSD1306.CurrentY + Font.FontHeight)) {
        return 0;
    }

    /* Recorrer las filas y columnas del glifo de la tipografía */
    for (uint32_t i = 0; i < Font.FontHeight; i++) {
        /* Restar 32 porque la tabla ASCII imprimible arranca en el espacio ' ' (ASCII 32) */
        uint16_t b = Font.data[(ch - 32) * Font.FontHeight + i];

        for (uint32_t j = 0; j < Font.FontWidth; j++) {
            if ((b << j) & 0x8000) {
                ssd1306_DrawPixel(SSD1306.CurrentX + j, SSD1306.CurrentY + i, color);
            } else {
                ssd1306_DrawPixel(SSD1306.CurrentX + j, SSD1306.CurrentY + i, (SSD1306_COLOR)!color);
            }
        }
    }

    /* Avanzar cursor a la siguiente columna */
    SSD1306.CurrentX += Font.FontWidth;
    return ch;
}

/**
 * @brief  Escribe una cadena de caracteres completa.
 */
char ssd1306_WriteString(char* str, FontDef Font, SSD1306_COLOR color) {
    while (*str) {
        if (ssd1306_WriteChar(*str, Font, color) != *str) {
            return *str; // Fin si desborda la pantalla
        }
        str++;
    }
    return *str;
}
