#ifndef USB_HONDA_HTS_H
#define USB_HONDA_HTS_H

#include <stdint.h>
#include <stdbool.h>

#define HONDA_HTS_ROM_SIZE 32768u
#define HONDA_HTS_BLOCK_SIZE 256u
#define HONDA_HTS_TIMEOUT_MS 250u

typedef enum {
    HTS_IDLE = 0,
    HTS_V_2ND,
    HTS_B_R1,
    HTS_B_R2,
    HTS_B_CKSUM,
    HTS_S_PARAM,
    HTS_S_CKSUM,
    HTS_Z_CMD,
    HTS_ZW_HDR,
    HTS_ZW_DATA,
    HTS_ZW_CKSUM,
    HTS_ZR_HDR,
    HTS_ZR_CKSUM
} hts_state_t;

typedef struct {
    hts_state_t state;
    uint8_t hdr[3];
    uint8_t hdr_pos;
    uint8_t checksum;
    uint16_t address;
    uint32_t remaining;
    uint32_t write_pos;
    uint32_t deadline_ms;
    uint8_t active_slot;
    uint8_t staging_slot;
    bool slots_ready;
    uint32_t good_packets;
    uint32_t bad_packets;
    uint32_t timeouts;
} hts_context_t;

void usb_hts_init(void);
void usb_hts_task(void);
void usb_hts_rx(const uint8_t *data, uint32_t len);
void usb_hts_reset_parser(void);

#endif
