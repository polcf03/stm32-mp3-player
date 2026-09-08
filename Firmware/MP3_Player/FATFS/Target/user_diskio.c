/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    user_diskio.c
  * @brief   Controlador de bajo nivel (Driver SPI) para la integración de FatFs
  *          con tarjetas SD en la plataforma STM32F4.
  * @details Implementa la inicialización física por comandos nativos SD,
  *          negociación de protocolo en SPI y optimización por registros
  *          para alta tasa de transferencia de bloques PCM.
  * @author  polcf03
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "ff_gen_drv.h"
#include "main.h"

/* Manejador del periférico SPI1 instanciado en main.c */
extern SPI_HandleTypeDef hspi1;

/**
  * @defgroup SD_CS_Configuracion Control de Línea Chip Select
  * @{
  */
#define SD_CS_PORT GPIOB
#define SD_CS_PIN  GPIO_PIN_6

/** @brief Habilita la comunicación con la tarjeta SD (Línea CS en LOW) */
#define SELECT()   HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET)

/** @brief Deshabilita la comunicación con la tarjeta SD (Línea CS en HIGH) */
#define DESELECT() HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET)
/** @} */

/**
  * @defgroup SD_Comandos Conjunto de Comandos Estándar SPI para Tarjetas SD
  * @{
  */
#define CMD0   (0)           /*!< GO_IDLE_STATE: Resetea la tarjeta al estado Idle */
#define CMD1   (1)           /*!< SEND_OP_COND: Inicia la inicialización (Tarjetas MMC) */
#define CMD8   (8)           /*!< SEND_IF_COND: Verifica rango de voltaje (SD v2.0+) */
#define CMD9   (9)           /*!< SEND_CSD: Solicita los datos específicos de la tarjeta */
#define CMD10  (10)          /*!< SEND_CID: Solicita la identificación del chip */
#define CMD12  (12)          /*!< STOP_TRANSMISSION: Detiene la lectura continua multibloque */
#define CMD17  (17)          /*!< READ_SINGLE_BLOCK: Lee un bloque individual de 512 bytes */
#define CMD18  (18)          /*!< READ_MULTIPLE_BLOCK: Inicia lectura continua de múltiples bloques */
#define CMD24  (24)          /*!< WRITE_BLOCK: Escribe un bloque de 512 bytes */
#define CMD55  (55)          /*!< APP_CMD: Prefijo para comandos específicos de aplicación (ACMD) */
#define CMD58  (58)          /*!< READ_OCR: Lee el registro de condiciones de operación */
#define ACMD41 (0x80 + 41)   /*!< SD_SEND_OP_COND: Inicia la inicialización de tarjetas SDHC/SDXC */
/** @} */

static volatile DSTATUS Stat = STA_NOINIT; /*!< Estado global del driver de disco */
static BYTE CardType;                      /*!< Tipo de tarjeta detectada (SDv1, SDv2, SDHC/SDXC) */

/**
  * @brief  Envía y recibe simultáneamente un byte mediante el bus SPI.
  * @param  data: Byte a transmitir hacia la tarjeta.
  * @retval Byte recibido desde la tarjeta SD.
  */
static BYTE SPI_TxRx(BYTE data) {
    BYTE rxData = 0xFF;
    HAL_SPI_TransmitReceive(&hspi1, &data, &rxData, 1, HAL_MAX_DELAY);
    return rxData;
}

/**
  * @brief  Espera hasta que la tarjeta SD libere la línea MISO (Estado Ready: 0xFF).
  * @retval 0xFF si la tarjeta está lista; de lo contrario, un valor distinto tras tiempo de espera.
  */
static BYTE SD_ReadyWait(void) {
    BYTE res;
    uint32_t timeout = 50000;
    do {
        res = SPI_TxRx(0xFF);
    } while ((res != 0xFF) && --timeout);
    return res;
}

/**
  * @brief  Escribe una trama de comando completa a la tarjeta SD según la especificación SPI.
  * @param  cmd: Código del comando (incluye máscara para ACMD).
  * @param  arg: Argumento de 32 bits asociado al comando.
  * @retval Byte de respuesta de la tarjeta (R1 status).
  */
static BYTE SD_SendCmd(BYTE cmd, DWORD arg) {
    BYTE res, n;

    /* Gestión de comandos extendidos ACMD */
    if (cmd & 0x80) {
        cmd &= 0x7F;
        res = SD_SendCmd(CMD55, 0);
        if (res > 1) return res;
    }

    /* Preparación del bus */
    DESELECT();
    SPI_TxRx(0xFF);
    SELECT();

    if (SD_ReadyWait() != 0xFF) return 0xFF;

    /* Transmisión de trama: [Comando | 0x40] [Arg 31..24] [Arg 23..16] [Arg 15..8] [Arg 7..0] [CRC] */
    SPI_TxRx(cmd | 0x40);
    SPI_TxRx((BYTE)(arg >> 24));
    SPI_TxRx((BYTE)(arg >> 16));
    SPI_TxRx((BYTE)(arg >> 8));
    SPI_TxRx((BYTE)arg);

    /* Formateo de suma de comprobación (CRC) requerida en la fase de inicialización */
    n = 0x01;
    if (cmd == CMD0) n = 0x95;  /* CRC precalculado para CMD0(0) */
    if (cmd == CMD8) n = 0x87;  /* CRC precalculado para CMD8(0x1AA) */
    SPI_TxRx(n);

    /* Espera de la respuesta R1 de la tarjeta (Bit MSB en 0) */
    n = 100;
    do {
        res = SPI_TxRx(0xFF);
    } while ((res & 0x80) && --n);

    return res;
}

/**
  * @brief  Inicializa la tarjeta SD física y establece la arquitectura del medio.
  * @param  pdrv: Número de unidad física (Solo se soporta la unidad 0).
  * @retval DSTATUS Estado resultante de la tarjeta (0 = OK, STA_NOINIT = Error).
  */
DSTATUS USER_initialize(BYTE pdrv) {
    BYTE n, ty, ocr[4];
    uint32_t timeout;

    if (pdrv) return STA_NOINIT;

    DESELECT();
    /* Enviar al menos 80 pulsos de reloj (0xFF) con CS en HIGH para activar la interfaz SPI de la SD */
    for (n = 20; n; n--) SPI_TxRx(0xFF);

    ty = 0;
    /* Transición a estado Idle */
    if (SD_SendCmd(CMD0, 0) == 1) {
        /* Verificación de versión de la tarjeta (SD v2.0+) */
        if (SD_SendCmd(CMD8, 0x1AA) == 1) {
            for (n = 0; n < 4; n++) ocr[n] = SPI_TxRx(0xFF);
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) { /* Rango de voltaje soportado: 2.7V - 3.6V */
                timeout = 10000;
                while (--timeout && SD_SendCmd(ACMD41, 1UL << 30));
                if (timeout && SD_SendCmd(CMD58, 0) == 0) {
                    for (n = 0; n < 4; n++) ocr[n] = SPI_TxRx(0xFF);
                    ty = (ocr[0] & 0x40) ? 12 : 4; /* Identificar direccionamiento por bloques (SDHC/SDXC) */
                }
            }
        } else {
            /* SD v1.x o MMC */
            if (SD_SendCmd(ACMD41, 0) <= 1) {
                ty = 2; /* SD v1 */
                timeout = 10000;
                while (--timeout && SD_SendCmd(ACMD41, 0));
            } else {
                ty = 1; /* MMC */
                timeout = 10000;
                while (--timeout && SD_SendCmd(CMD1, 0));
            }
            if (!timeout) ty = 0;
        }
    }

    CardType = ty;
    DESELECT();
    SPI_TxRx(0xFF);

    if (ty) {
        Stat &= ~STA_NOINIT;
    } else {
        Stat = STA_NOINIT;
    }
    return Stat;
}

/**
  * @brief  Devuelve el estado de la unidad de disco.
  * @param  pdrv: Número de unidad física.
  * @retval DSTATUS Estado de inicialización.
  */
DSTATUS USER_status(BYTE pdrv) {
    if (pdrv) return STA_NOINIT;
    return Stat;
}

/**
  * @brief  Lee uno o varios sectores de 512 bytes desde la tarjeta SD.
  * @note   Utiliza acceso directo a registros del controlador SPI1 (`SPI1->DR`)
  *         para minimizar la latencia por ráfaga (reducido de ~3.5 ms a ~0.38 ms).
  * @param  pdrv: Número de unidad física.
  * @param  buff: Puntero al búfer de destino en RAM donde alojar los datos.
  * @param  sector: Dirección lógica del sector a leer.
  * @param  count: Número total de sectores a leer en ráfaga.
  * @retval DRESULT Resultado de la operación FatFs.
  */
DRESULT USER_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count) {
    if (pdrv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    /* Conversión a dirección por bytes si la tarjeta no es SDHC/SDXC */
    if (!(CardType & 8)) sector *= 512;

    BYTE cmd = (count > 1) ? CMD18 : CMD17;
    if (SD_SendCmd(cmd, sector) == 0) {
        do {
            uint32_t timeout = 40000;
            /* Esperar el token de inicio de datos (Data Token: 0xFE) */
            while ((SPI_TxRx(0xFF) != 0xFE) && --timeout);
            if (timeout == 0) break;

            /* LECTURA ULTRARRÁPIDA DIRECTA POR REGISTROS HARDFILE SPI1 */
            for (UINT i = 0; i < 512; i++) {
                while (!(SPI1->SR & SPI_SR_TXE));  /*!< Esperar a que el búfer TX esté libre */
                SPI1->DR = 0xFF;                   /*!< Transmitir byte dummy para generar el reloj */
                while (!(SPI1->SR & SPI_SR_RXNE)); /*!< Esperar a la recepción del byte del bus */
                *buff++ = (BYTE)SPI1->DR;          /*!< Guardar el dato recibido directamente */
            }

            /* Consumo y descarte rápido de los 2 bytes de CRC enviado por la SD */
            while (!(SPI1->SR & SPI_SR_TXE));
            SPI1->DR = 0xFF;
            while (!(SPI1->SR & SPI_SR_RXNE));
            (void)SPI1->DR;

            while (!(SPI1->SR & SPI_SR_TXE));
            SPI1->DR = 0xFF;
            while (!(SPI1->SR & SPI_SR_RXNE));
            (void)SPI1->DR;

        } while (--count);

        if (cmd == CMD18) {
            SD_SendCmd(CMD12, 0); /* Emitir parada de transmisión tras streaming continuo */
        }
    }

    DESELECT();
    SPI_TxRx(0xFF);
    return (count == 0) ? RES_OK : RES_ERROR;
}

#if _USE_WRITE == 1
/**
  * @brief  Escribe un sector de 512 bytes en la tarjeta SD.
  * @param  pdrv: Número de unidad física.
  * @param  buff: Puntero al búfer de datos en RAM a escribir.
  * @param  sector: Dirección del sector lógica.
  * @param  count: Cantidad de sectores a escribir.
  * @retval DRESULT Resultado de la operación.
  */
DRESULT USER_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count) {
    if (pdrv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    if (!(CardType & 8)) sector *= 512;

    if (count == 1) {
        if (SD_SendCmd(CMD24, sector) == 0) {
            SPI_TxRx(0xFF);
            SPI_TxRx(0xFE); /* Token de inicio de bloque */
            for (UINT i = 0; i < 512; i++) SPI_TxRx(buff[i]);
            SPI_TxRx(0xFF); /* CRC Dummy Byte 1 */
            SPI_TxRx(0xFF); /* CRC Dummy Byte 2 */
            if ((SPI_TxRx(0xFF) & 0x1F) == 0x05) count = 0; /* Verificar aceptación de datos */
        }
    }

    DESELECT();
    SPI_TxRx(0xFF);
    return count ? RES_ERROR : RES_OK;
}
#endif

#if _USE_IOCTL == 1
/**
  * @brief  Control de funciones e información miscelánea del dispositivo de almacenamiento.
  * @param  pdrv: Número de unidad física.
  * @param  cmd: Código de control/comando de entrada.
  * @param  buff: Puntero al búfer de parámetros o retorno de datos.
  * @retval DRESULT Resultado de la operación IOCTL.
  */
DRESULT USER_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    DRESULT res = RES_ERROR;
    switch (cmd) {
        case CTRL_SYNC:
            SELECT();
            if (SD_ReadyWait() == 0xFF) res = RES_OK;
            break;
        case GET_SECTOR_SIZE:
            *(WORD*)buff = 512;
            res = RES_OK;
            break;
        case GET_BLOCK_SIZE:
            *(DWORD*)buff = 32;
            res = RES_OK;
            break;
        case GET_SECTOR_COUNT:
            *(DWORD*)buff = 100000;
            res = RES_OK;
            break;
        default:
            res = RES_PARERR;
            break;
    }
    DESELECT();
    SPI_TxRx(0xFF);
    return res;
}
#endif

/**
  * @brief Estructura de registro del driver FatFs con sus respectivas funciones de callback.
  */
Diskio_drvTypeDef USER_Driver = {
    USER_initialize,
    USER_status,
    USER_read,
#if _USE_WRITE
    USER_write,
#endif
#if _USE_IOCTL == 1
    USER_ioctl,
#endif
};
