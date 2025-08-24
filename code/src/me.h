#ifndef _ME_H_
#define _ME_H_

#include <pico/unique_id.h>

#define USB_VID 0xda1e
#define USB_PID 0xB0CA

extern const char manufacturer[];
extern const char product[];
extern const char version[];
extern char serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

void me_init();

#endif
