/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    user_diskio.c
  * @brief   Driver SPI de bajo nivel para FatFs en STM32F4 (CS en PB6)
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "ff_gen_drv.h"
#include "main.h"

/* Handle del periférico SPI1 definido en main.c */
extern SPI_HandleTypeDef hspi1;

/* Configuración del Pin CS (Chip Select) de la SD en PB6 */
#define SD_CS_PORT GPIOB
#define SD_CS_PIN  GPIO_PIN_6

#define SELECT()   HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET)
#define DESELECT() HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET)

/* Comandos estándar de la tarjeta SD en modo SPI */
#define CMD0   (0)           /* GO_IDLE_STATE */
#define CMD1   (1)           /* SEND_OP_COND */
#define CMD8   (8)           /* SEND_IF_COND */
#define CMD9   (9)           /* SEND_CSD */
#define CMD10  (10)          /* SEND_CID */
#define CMD12  (12)          /* STOP_TRANSMISSION */
#define CMD17  (17)          /* READ_SINGLE_BLOCK */
#define CMD18  (18)          /* READ_MULTIPLE_BLOCK */
#define CMD24  (24)          /* WRITE_BLOCK */
#define CMD55  (55)          /* APP_CMD */
#define CMD58  (58)          /* READ_OCR */
#define ACMD41 (0x80 + 41)   /* SD_SEND_OP_COND */

static volatile DSTATUS Stat = STA_NOINIT;
static BYTE CardType;        /* Tipo de tarjeta: SDv1, SDv2 o Block-Addressing */

/* Enviar y recibir 1 byte vía SPI */
static BYTE SPI_TxRx(BYTE data) {
    BYTE rxData = 0xFF;
    HAL_SPI_TransmitReceive(&hspi1, &data, &rxData, 1, HAL_MAX_DELAY);
    return rxData;
}

/* Esperar a que la tarjeta SD responda (Ready) */
static BYTE SD_ReadyWait(void) {
    BYTE res;
    uint32_t timeout = 50000;
    do {
        res = SPI_TxRx(0xFF);
    } while ((res != 0xFF) && --timeout);
    return res;
}

/* Enviar un comando a la tarjeta SD */
static BYTE SD_SendCmd(BYTE cmd, DWORD arg) {
    BYTE res, n;

    if (cmd & 0x80) {
        cmd &= 0x7F;
        res = SD_SendCmd(CMD55, 0);
        if (res > 1) return res;
    }

    DESELECT();
    SPI_TxRx(0xFF);
    SELECT();

    if (SD_ReadyWait() != 0xFF) return 0xFF;

    SPI_TxRx(cmd | 0x40);
    SPI_TxRx((BYTE)(arg >> 24));
    SPI_TxRx((BYTE)(arg >> 16));
    SPI_TxRx((BYTE)(arg >> 8));
    SPI_TxRx((BYTE)arg);

    n = 0x01;
    if (cmd == CMD0) n = 0x95;  /* CRC válido para CMD0 */
    if (cmd == CMD8) n = 0x87;  /* CRC válido para CMD8(0x1AA) */
    SPI_TxRx(n);

    n = 100;
    do {
        res = SPI_TxRx(0xFF);
    } while ((res & 0x80) && --n);

    return res;
}

/* Inicializar la tarjeta SD */
DSTATUS USER_initialize(BYTE pdrv) {
    BYTE n, ty, ocr[4];
    uint32_t timeout;

    if (pdrv) return STA_NOINIT;

    DESELECT();
    /* Enviar al menos 80 pulsos de reloj (0xFF) con CS en HIGH para despertar la SD en modo SPI */
    for (n = 20; n; n--) SPI_TxRx(0xFF);

    ty = 0;
    if (SD_SendCmd(CMD0, 0) == 1) {
        if (SD_SendCmd(CMD8, 0x1AA) == 1) {
            for (n = 0; n < 4; n++) ocr[n] = SPI_TxRx(0xFF);
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
                timeout = 10000;
                while (--timeout && SD_SendCmd(ACMD41, 1UL << 30));
                if (timeout && SD_SendCmd(CMD58, 0) == 0) {
                    for (n = 0; n < 4; n++) ocr[n] = SPI_TxRx(0xFF);
                    ty = (ocr[0] & 0x40) ? 12 : 4;
                }
            }
        } else {
            if (SD_SendCmd(ACMD41, 0) <= 1) {
                ty = 2;
                timeout = 10000;
                while (--timeout && SD_SendCmd(ACMD41, 0));
            } else {
                ty = 1;
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

DSTATUS USER_status(BYTE pdrv) {
    if (pdrv) return STA_NOINIT;
    return Stat;
}

/* Leer sectores de la tarjeta mediante streaming multiblock ultrarrápido */
DRESULT USER_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count) {
    if (pdrv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    if (!(CardType & 8)) sector *= 512;

    BYTE cmd = (count > 1) ? CMD18 : CMD17;
    if (SD_SendCmd(cmd, sector) == 0) {
        do {
            uint32_t timeout = 40000;
            while ((SPI_TxRx(0xFF) != 0xFE) && --timeout);
            if (timeout == 0) break;

            /* Lectura ultra-rápida por registros directos (0.38 ms por sector en lugar de 3.5 ms) */
            for (UINT i = 0; i < 512; i++) {
                while (!(SPI1->SR & SPI_SR_TXE));
                SPI1->DR = 0xFF;
                while (!(SPI1->SR & SPI_SR_RXNE));
                *buff++ = (BYTE)SPI1->DR;
            }

            /* Descartar los 2 bytes de CRC rápidamente */
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
            SD_SendCmd(CMD12, 0); /* Detener el streaming continuo */
        }
    }

    DESELECT();
    SPI_TxRx(0xFF);
    return (count == 0) ? RES_OK : RES_ERROR;
}

#if _USE_WRITE == 1
DRESULT USER_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count) {
    if (pdrv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    if (!(CardType & 8)) sector *= 512;

    if (count == 1) {
        if (SD_SendCmd(CMD24, sector) == 0) {
            SPI_TxRx(0xFF);
            SPI_TxRx(0xFE);
            for (UINT i = 0; i < 512; i++) SPI_TxRx(buff[i]);
            SPI_TxRx(0xFF);
            SPI_TxRx(0xFF);
            if ((SPI_TxRx(0xFF) & 0x1F) == 0x05) count = 0;
        }
    }

    DESELECT();
    SPI_TxRx(0xFF);
    return count ? RES_ERROR : RES_OK;
}
#endif

#if _USE_IOCTL == 1
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
