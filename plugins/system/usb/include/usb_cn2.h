#ifndef USB_CN2_H
#define USB_CN2_H
#include <stdint.h>
void usb_cn2_init(void);
void usb_cn2_task(void);
void usb_cn2_cdc_rx(const uint8_t *data, uint32_t len);
#endif
