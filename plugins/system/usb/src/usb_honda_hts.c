#include "usb_plugin.h"
#include "usb_honda_hts.h"
#include "tusb.h"

static hts_context_t hts;
static bool hts_tx_pending = false;
static uint8_t hts_tx_pending_byte = 0;

#define HTS_TRACE_SIZE 32
static uint8_t hts_trace[HTS_TRACE_SIZE];
static uint8_t hts_trace_len = 0;

static void hts_try_pending_reply(void) {
    if (!hts_tx_pending) return;
    if (!tud_cdc_n_connected(0)) return;
    if (!tud_cdc_n_write_available(0)) return;

    if (tud_cdc_n_write(0, &hts_tx_pending_byte, 1) == 1) {
        tud_cdc_n_write_flush(0);
        hts_tx_pending = false;
    }
}
static void hts_parser_idle(void) {
    hts.state = HTS_IDLE;
    hts.hdr_pos = 0;
    hts.checksum = 0;
    hts.address = 0;
    hts.remaining = 0;
    hts.write_pos = 0;
    hts.deadline_ms = 0;
}

void usb_hts_reset_parser(void) {
    hts_parser_idle();
}

static bool hts_range_ok(uint32_t addr, uint32_t len) {
    return addr < HONDA_HTS_ROM_SIZE && len <= (HONDA_HTS_ROM_SIZE - addr);
}

static bool hts_get_slots(void) {
    uint8_t active = 0;
    if (context.get_active_ram_slot(&active) != ORA_RESULT_OK) return false;
    uint8_t count = context.get_ram_slot_count ? context.get_ram_slot_count() : 0;
    if (count < 2) return false;
    uint8_t staging = (active == 0) ? 1 : 0;
    uint32_t active_size = 0, staging_size = 0;
    if (context.get_ram_slot_info(active, NULL, &active_size, NULL) != ORA_RESULT_OK) return false;
    if (context.get_ram_slot_info(staging, NULL, &staging_size, NULL) != ORA_RESULT_OK) return false;
    if (active_size < HONDA_HTS_ROM_SIZE || staging_size < HONDA_HTS_ROM_SIZE) return false;
    hts.active_slot = active;
    hts.staging_slot = staging;
    hts.slots_ready = true;
    return true;
}

static bool hts_copy_range(uint8_t src, uint8_t dst, uint32_t off, uint32_t len) {
    uint8_t buf[64];
    while (len) {
        uint32_t n = len > sizeof(buf) ? sizeof(buf) : len;
        if (context.read_ram_rom_slot(src, off, buf, n) != ORA_RESULT_OK) return false;
        if (context.reprogram_ram_rom_slot(dst, off, buf, n, 0) != ORA_RESULT_OK) return false;
        off += n;
        len -= n;
    }
    return true;
}

static bool hts_stage_write_byte(uint32_t off, uint8_t b) {
    return context.reprogram_ram_rom_slot(hts.staging_slot, off, &b, 1, 0) == ORA_RESULT_OK;
}

static void hts_reply_byte(uint8_t b) {
    hts_tx_pending_byte = b;
    hts_tx_pending = true;
    hts_try_pending_reply();
}

static void hts_fail_write(void) {
    /* Roll back the touched range from the still-live image. */
    if (hts.slots_ready && hts.write_pos >= hts.address) {
        (void)hts_copy_range(hts.active_slot, hts.staging_slot,
                             hts.address, hts.write_pos - hts.address);
    }
    hts.bad_packets++;
    hts_parser_idle();
}

static void hts_commit_write(void) {
    uint32_t len = hts.write_pos - hts.address;
    if (context.set_active_ram_slot(hts.staging_slot) != ORA_RESULT_OK) {
        hts_fail_write();
        return;
    }

    uint8_t old_active = hts.active_slot;
    hts.active_slot = hts.staging_slot;
    hts.staging_slot = old_active;

    /* Keep the inactive slot mirrored for the next atomic update. */
    (void)hts_copy_range(hts.active_slot, hts.staging_slot, hts.address, len);

    hts.good_packets++;
    hts_reply_byte('O');
    hts_parser_idle();
}

static bool hts_begin_stream(void) {
    uint8_t count = hts.hdr[0];
    uint16_t addr = (uint16_t)(((uint16_t)hts.hdr[1] << 8) | hts.hdr[2]);
    uint32_t total = (uint32_t)count * HONDA_HTS_BLOCK_SIZE;
    if (count == 0 || !hts_range_ok(addr, total)) return false;
    if (!hts.slots_ready && !hts_get_slots()) return false;
    hts.address = addr;
    hts.write_pos = addr;
    hts.remaining = total;
    return true;
}

static void hts_feed(uint8_t b) {
    hts.deadline_ms = context.timer_ms + HONDA_HTS_TIMEOUT_MS;

    switch (hts.state) {
    case HTS_IDLE:
    if (b == 'V') hts.state = HTS_V_2ND;
        case HTS_N_S:
    if (b == 'S') {
        hts.state = HTS_N_CKSUM;
    } else {
        hts.bad_packets++;
        hts_parser_idle();
    }
    break;

case HTS_N_CKSUM:
    if (b == 0xA1) {
        static const uint8_t ns_reply[10] = {
            0x00,
            0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF,
            0x99
        };

        if (tud_cdc_n_write_available(0) >= sizeof(ns_reply)) {
            tud_cdc_n_write(0, ns_reply, sizeof(ns_reply));
            tud_cdc_n_write_flush(0);
        }

        hts.good_packets++;
    } else {
        hts.bad_packets++;
    }

    hts_parser_idle();
    break;
    else if (b == 'B') hts.state = HTS_B_R1;
    else if (b == 'S') hts.state = HTS_S_PARAM;
    else if (b == 'Z') hts.state = HTS_Z_CMD;
    break;
        case HTS_V_2ND:
    if (b == 'V') {
        static const uint8_t version_reply[3] = {
            0x14, 0x09, 0x4F
        };

        if (tud_cdc_n_write_available(0) >= 3) {
            tud_cdc_n_write(0, version_reply, 3);
            tud_cdc_n_write_flush(0);
        }

        hts.good_packets++;
    } else {
        hts.bad_packets++;
    }

    hts_parser_idle();
    break;

    case HTS_B_R1:
        if (b == 'R') hts.state = HTS_B_R2;
        else hts_parser_idle();
        break;
    case HTS_B_R2:
    if (b == 'R') {
        hts_reply_byte(0x00);
        hts.good_packets++;
        hts.state = HTS_B_CKSUM;
    } else {
        hts_parser_idle();
    }
    break;
    case HTS_B_CKSUM:
    /* HTS sends the checksum after BRR. Swallow it silently. */
    if (b != 0xE6u) {
        hts.bad_packets++;
    }
    hts_parser_idle();
    break;

    case HTS_S_PARAM:
        hts.checksum = (uint8_t)('S' + b);
        hts.state = HTS_S_CKSUM;
        break;
    case HTS_S_CKSUM:
        if (b == hts.checksum) hts.good_packets++;
        else hts.bad_packets++;
        hts_parser_idle();
        break;

    case HTS_Z_CMD:
        hts.hdr_pos = 0;
        hts.checksum = (uint8_t)('Z' + b);
        if (b == 'W') hts.state = HTS_ZW_HDR;
        else if (b == 'R') hts.state = HTS_ZR_HDR;
        else { hts.bad_packets++; hts_parser_idle(); }
        break;

    case HTS_ZW_HDR:
        hts.hdr[hts.hdr_pos++] = b;
        hts.checksum = (uint8_t)(hts.checksum + b);
        if (hts.hdr_pos == 3) {
            if (!hts_begin_stream()) { hts_fail_write(); break; }
            hts.state = HTS_ZW_DATA;
        }
        break;

    case HTS_ZW_DATA:
        if (!hts.remaining || !hts_stage_write_byte(hts.write_pos, b)) {
            hts_fail_write();
            break;
        }
        hts.checksum = (uint8_t)(hts.checksum + b);
        hts.write_pos++;
        hts.remaining--;
        if (!hts.remaining) hts.state = HTS_ZW_CKSUM;
        break;

    case HTS_ZW_CKSUM:
        if (b != hts.checksum) hts_fail_write();
        else hts_commit_write();
        break;

    case HTS_ZR_HDR:
        hts.hdr[hts.hdr_pos++] = b;
        hts.checksum = (uint8_t)(hts.checksum + b);
        if (hts.hdr_pos == 3) {
            uint8_t count = hts.hdr[0];
            uint16_t addr = (uint16_t)(((uint16_t)hts.hdr[1] << 8) | hts.hdr[2]);
            uint32_t total = (uint32_t)count * HONDA_HTS_BLOCK_SIZE;
            if (!count || !hts_range_ok(addr, total)) {
                hts.bad_packets++;
                hts_parser_idle();
                break;
            }
            hts.address = addr;
            hts.remaining = total;
            hts.state = HTS_ZR_CKSUM;
        }
        break;

    case HTS_ZR_CKSUM:
        /* Host checksum terminates the request; task then streams data + checksum. */
        if (b != hts.checksum) {
            hts.bad_packets++;
            hts_parser_idle();
        } else {
            hts.good_packets++;
            hts.deadline_ms = 0;
        }
        break;
    }
}

void usb_hts_init(void) {
    hts_tx_pending = false; 
    hts.state = HTS_IDLE;
    hts.slots_ready = false;
    hts.good_packets = hts.bad_packets = hts.timeouts = 0;
    if (hts_get_slots()) {
        /* The inactive slot is uninitialised at boot. Mirror the complete live ROM
         * before it can ever become active. */
        if (!hts_copy_range(hts.active_slot, hts.staging_slot, 0, HONDA_HTS_ROM_SIZE)) {
            hts.slots_ready = false;
        }
    }
}

void usb_hts_rx(const uint8_t *data, uint32_t len) {
    /*
     * Diagnostic commands:
     * F1 = clear trace
     * F0 = return trace: first byte is length, followed by captured bytes
     */
    if (len == 1 && data[0] == 0xF1) {
        hts_trace_len = 0;
        return;
    }

    if (len == 1 && data[0] == 0xF0) {
        if (tud_cdc_n_write_available(0) >= (uint32_t)(hts_trace_len + 1)) {
            tud_cdc_n_write_char(0, (char)hts_trace_len);

            if (hts_trace_len) {
                tud_cdc_n_write(0, hts_trace, hts_trace_len);
            }

            tud_cdc_n_write_flush(0);
        }
        return;
    }

    for (uint32_t i = 0; i < len; ++i) {
        if (hts_trace_len < HTS_TRACE_SIZE) {
            hts_trace[hts_trace_len++] = data[i];
        }

        hts_feed(data[i]);
    }
}

void usb_hts_task(void) {
    hts_try_pending_reply();
    
    if (hts.state != HTS_IDLE && hts.deadline_ms &&
        (int32_t)(context.timer_ms - hts.deadline_ms) >= 0) {
        hts.timeouts++;
        if (hts.state == HTS_ZW_DATA || hts.state == HTS_ZW_CKSUM) hts_fail_write();
        else hts_parser_idle();
    }

    if (hts.state == HTS_ZR_CKSUM && hts.remaining && tud_cdc_n_connected(0)) {
        uint8_t out[64];
        uint32_t avail = tud_cdc_n_write_available(0);
        uint32_t n = hts.remaining;
        if (n > sizeof(out)) n = sizeof(out);
        if (n > avail) n = avail;
        if (n) {
            if (context.read_ram_rom_slot(hts.active_slot, hts.address, out, n) != ORA_RESULT_OK) {
                hts.bad_packets++;
                hts_parser_idle();
                return;
            }
            for (uint32_t i = 0; i < n; ++i) hts.checksum = (uint8_t)(hts.checksum + out[i]);
            tud_cdc_n_write(0, out, n);
            tud_cdc_n_write_flush(0);
            hts.address += (uint16_t)n;
            hts.remaining -= n;
            if (!hts.remaining) {
                hts_reply_byte(hts.checksum);
                hts_parser_idle();
            }
        }
    }
}
