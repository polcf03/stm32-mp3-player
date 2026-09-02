#include "ssd1306.h"

/* ----------------------------------------------------------------------------
 * VARIABLES PRIVADAS Y MEMORIA RAM (FRAMEBUFFER)
 * ---------------------------------------------------------------------------- */

/* Framebuffer: Matriz en la RAM del STM32 que almacena la imagen de la pantalla.
 * Tamaño: (128 píxeles de ancho * 64 píxeles de alto) / 8 bits por byte = 1024 Bytes (1 KB).
 * "static" limita su visibilidad exclusivamente a este archivo .c */
static uint8_t SSD1306_Buffer[128 * 64 / 8];

/* Estructura para almacenar el estado del cursor y de inicialización */
typedef struct {
    uint16_t CurrentX;   /* Posición actual del cursor en el eje X (0-127) */
    uint16_t CurrentY;   /* Posición actual del cursor en el eje Y (0-63) */
    uint8_t Initialized; /* Bandera que indica si la pantalla ya fue inicializada (1) o no (0) */
} SSD1306_t;

/* Instancia de la estructura de control de estado */
static SSD1306_t SSD1306;

/* ----------------------------------------------------------------------------
 * FUNCIONES PRIVADAS DE COMUNICACIÓN I2C
 * ---------------------------------------------------------------------------- */

/**
  * @brief  Envía un byte de comando al controlador SSD1306.
  * @param  command: Byte de comando a transmitir (ej. 0xAF para encender pantalla).
  * @retval Ninguno.
  */
static void ssd1306_WriteCommand(uint8_t command) {
    /* HAL_I2C_Mem_Write argumentos:
     * 1. &SSD1306_I2C_PORT: Puntero al periférico I2C (hi2c1).
     * 2. SSD1306_I2C_ADDR: Dirección I2C del chip (0x78).
     * 3. 0x00: "Control Byte" -> Le indica a la pantalla que el byte siguiente es un COMANDO.
     * 4. 1: Tamaño de la dirección de memoria interna (1 Byte).
     * 5. &command: Puntero al byte del comando a enviar.
     * 6. 1: Cantidad de bytes a transmitir.
     * 7. 100: Tiempo de espera máximo (timeout en milisegundos). */
    HAL_I2C_Mem_Write(&SSD1306_I2C_PORT, SSD1306_I2C_ADDR, 0x00, 1, &command, 1, 100);
}

/* ----------------------------------------------------------------------------
 * FUNCIONES PÚBLICAS DE DIBUJO Y CONTROL
 * ---------------------------------------------------------------------------- */

/**
  * @brief  Inicializa el controlador de la pantalla OLED mediante la secuencia
  *         de comandos requerida por el fabricante.
  * @retval uint8_t: Retorna 1 al finalizar correctamente.
  */
uint8_t ssd1306_Init(void) {
    /* Pequeño retardo inicial para asegurar estabilidad de voltaje en la pantalla */
    HAL_Delay(100);

    /* Secuencia de comandos de inicialización del chip SSD1306 */
    ssd1306_WriteCommand(0xAE); // Display OFF (Apaga la pantalla durante la configuración)
    ssd1306_WriteCommand(0x20); // Establece el modo de direccionamiento de memoria
    ssd1306_WriteCommand(0x10); // Modo de direccionamiento de página
    ssd1306_WriteCommand(0xB0); // Establece la página de inicio para la memoria GDDRAM en 0
    ssd1306_WriteCommand(0xC8); // Escaneo de salida COM remapeado (orientación vertical)
    ssd1306_WriteCommand(0x00); // Establece la dirección de columna inicial (parte baja)
    ssd1306_WriteCommand(0x10); // Establece la dirección de columna inicial (parte alta)
    ssd1306_WriteCommand(0x40); // Establece la línea de inicio de pantalla a 0
    ssd1306_WriteCommand(0x81); // Configuración del contraste de la pantalla
    ssd1306_WriteCommand(0xFF); // Valor de contraste máximo (0x00 a 0xFF)
    ssd1306_WriteCommand(0xA1); // Establece el remapeo de columnas/segmentos (orientación horizontal)
    ssd1306_WriteCommand(0xA6); // Display Normal (píxeles encendidos = blanco, no invertido)
    ssd1306_WriteCommand(0xA8); // Configuración del Multiplex Ratio
    ssd1306_WriteCommand(0x3F); // Duty ratio de 1/64 (para pantallas de 64 píxeles de alto)
    ssd1306_WriteCommand(0xA4); // Sigue el contenido de la memoria RAM (Output follows RAM)
    ssd1306_WriteCommand(0xD3); // Configura el desplazamiento de pantalla (Display Offset)
    ssd1306_WriteCommand(0x00); // Sin desplazamiento (0x00)
    ssd1306_WriteCommand(0xD5); // Configura la frecuencia del oscilador/divisor de reloj
    ssd1306_WriteCommand(0xF0); // Ratio máximo
    ssd1306_WriteCommand(0xD9); // Configuración del periodo de Pre-carga
    ssd1306_WriteCommand(0x22); // Periodo recomendado
    ssd1306_WriteCommand(0xDA); // Configuración de pines COM del hardware
    ssd1306_WriteCommand(0x12); // Configuración alternativa para resolución 128x64
    ssd1306_WriteCommand(0xDB); // Configura el nivel de deselección VCOMH
    ssd1306_WriteCommand(0x20); // 0.77 * Vcc
    ssd1306_WriteCommand(0x8D); // Habilita el elevador de voltaje interno (Charge Pump)
    ssd1306_WriteCommand(0x14); // Genera los 7V-9V necesarios desde los 3.3V de entrada
    ssd1306_WriteCommand(0xAF); // Display ON (Enciende la pantalla finalmente)

    /* Limpia el buffer local de RAM y reinicia las coordenadas del cursor */
    ssd1306_Fill(Black);
    SSD1306.CurrentX = 0;
    SSD1306.CurrentY = 0;
    SSD1306.Initialized = 1;

    return 1;
}

/**
  * @brief  Rellena todo el buffer con el color indicado (Black o White).
  * @param  color: Color a pintar (Black = 0x00, White = 0xFF).
  * @retval Ninguno.
  */
void ssd1306_Fill(SSD1306_COLOR color) {
    /* Si el color es Black pone los bytes a 0x00, si es White pone los bytes a 0xFF */
    uint8_t fill_val = (color == Black) ? 0x00 : 0xFF;

    for(uint16_t i = 0; i < sizeof(SSD1306_Buffer); i++) {
        SSD1306_Buffer[i] = fill_val;
    }
}

/**
  * @brief  Transfiere el buffer de la RAM del STM32 a la memoria física del chip SSD1306.
  * @retval Ninguno.
  */
void ssd1306_UpdateScreen(void) {
    /* La pantalla está dividida en 8 páginas horizontales de 8 píxeles de alto cada una */
    for(uint8_t i = 0; i < 8; i++) {
        ssd1306_WriteCommand(0xB0 + i); // Establece la dirección de la página (0xB0 a 0xB7)
        ssd1306_WriteCommand(0x00);      // Resetea la columna inicio (Nibble bajo)
        ssd1306_WriteCommand(0x10);      // Resetea la columna inicio (Nibble alto)

        /* 0x40 es el byte de control que indica transmisión de DATOS DE PÍXEL.
         * Envía un bloque de 128 bytes correspondientes a la página actual. */
        HAL_I2C_Mem_Write(&SSD1306_I2C_PORT, SSD1306_I2C_ADDR, 0x40, 1, &SSD1306_Buffer[128 * i], 128, 100);
    }
}

/**
  * @brief  Modifica un único píxel en las coordenadas (X,Y) dentro del buffer de RAM.
  * @param  x: Posición horizontal (0 a 127).
  * @param  y: Posición vertical (0 a 63).
  * @param  color: Estado del píxel (White = Encendido, Black = Apagado).
  * @retval Ninguno.
  */
void ssd1306_DrawPixel(uint8_t x, uint8_t y, SSD1306_COLOR color) {
    /* Verificación de límites para evitar corrupción de memoria */
    if(x >= 128 || y >= 64) {
        return;
    }

    /* Modifica el bit específico dentro del byte correspondiente en la matriz */
    if(color == White) {
        SSD1306_Buffer[x + (y / 8) * 128] |= (1 << (y % 8));  // Activa el bit (1) mediante OR
    } else {
        SSD1306_Buffer[x + (y / 8) * 128] &= ~(1 << (y % 8)); // Desactiva el bit (0) mediante AND
    }
}

/**
  * @brief  Establece la coordenada actual donde se comenzará a escribir texto.
  * @param  x: Coordenada X (0-127).
  * @param  y: Coordenada Y (0-63).
  * @retval Ninguno.
  */
void ssd1306_SetCursor(uint8_t x, uint8_t y) {
    SSD1306.CurrentX = x;
    SSD1306.CurrentY = y;
}

/**
  * @brief  Dibuja un solo carácter en la posición actual del cursor.
  * @param  ch: Carácter ASCII a dibujar (ej. 'A').
  * @param  Font: Estructura con la fuente de texto a utilizar.
  * @param  color: Color de las letras.
  * @retval char: Carácter escrito o 0 si no cupo en la pantalla.
  */
char ssd1306_WriteChar(char ch, FontDef Font, SSD1306_COLOR color) {
    uint32_t i, b, j;

    /* Comprueba si el carácter cabe en los márgenes de la pantalla */
    if (128 < (SSD1306.CurrentX + Font.FontWidth) ||
        64 < (SSD1306.CurrentY + Font.FontHeight)) {
        return 0;
    }

    /* Recorre la matriz de píxeles correspondiente a la fuente seleccionada */
    for(i = 0; i < Font.FontHeight; i++) {
        /* Resta 32 al valor ASCII ya que las tablas de fuentes inician en el espacio en blanco ' ' (ASCII 32) */
        b = Font.data[(ch - 32) * Font.FontHeight + i];

        for(j = 0; j < Font.FontWidth; j++) {
            /* Evalúa bit a bit si el punto de la letra debe ir encendido o apagado */
            if((b << j) & 0x8000) {
                ssd1306_DrawPixel(SSD1306.CurrentX + j, SSD1306.CurrentY + i, color);
            } else {
                ssd1306_DrawPixel(SSD1306.CurrentX + j, SSD1306.CurrentY + i, (SSD1306_COLOR)!color);
            }
        }
    }

    /* Desplaza el cursor X hacia la derecha para la siguiente letra */
    SSD1306.CurrentX += Font.FontWidth;

    return ch;
}

/**
  * @brief  Escribe una cadena de caracteres completa.
  * @param  str: Cadena de texto a escribir (cadena terminada en '\0').
  * @param  Font: Estructura de la fuente tipográfica.
  * @param  color: Color del texto.
  * @retval char: Retorna el último carácter procesado.
  */
char ssd1306_WriteString(char* str, FontDef Font, SSD1306_COLOR color) {
    /* Bucle hasta encontrar el carácter nulo '\0' que marca el final del string */
    while (*str) {
        if (ssd1306_WriteChar(*str, Font, color) != *str) {
            return *str; // Retorna si ocurrió un error al escribir el carácter
        }
        str++; // Avanza al siguiente carácter del puntero
    }
    return *str;
}
