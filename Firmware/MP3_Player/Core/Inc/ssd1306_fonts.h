#ifndef __SSD1306_FONTS_H__
#define __SSD1306_FONTS_H__

#include <stdint.h>

typedef struct {
    const uint8_t FontWidth;
    uint8_t FontHeight;
    const uint16_t *data;
} FontDef;

extern FontDef Font_7x10;


#endif
