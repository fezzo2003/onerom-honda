#include "usb_plugin.h"
#include "usb_cn2.h"
#include "tusb.h"
#include "reg-rp235x.h"

#define UART1_BASE 0x40078000u
#define UART_DR      (*(volatile uint32_t *)(UART1_BASE + 0x000u))
#define UART_FR      (*(volatile uint32_t *)(UART1_BASE + 0x018u))
#define UART_IBRD    (*(volatile uint32_t *)(UART1_BASE + 0x024u))
#define UART_FBRD    (*(volatile uint32_t *)(UART1_BASE + 0x028u))
#define UART_LCR_H   (*(volatile uint32_t *)(UART1_BASE + 0x02Cu))
#define UART_CR      (*(volatile uint32_t *)(UART1_BASE + 0x030u))
#define UART_ICR     (*(volatile uint32_t *)(UART1_BASE + 0x044u))

#define UART_FR_TXFF (1u << 5)
#define UART_FR_RXFE (1u << 4)
#define RESET_UART1  (1u << 27)
#define GPIO_FUNC_UART1_AUX 0x0Bu
#define CN2_TX_GPIO 26u
#define CN2_RX_GPIO 27u
#define CN2_BAUD 38400u

static void uart_set_baud(uint32_t clk_hz, uint32_t baud) {
    uint32_t div_x64 = (clk_hz * 4u + baud / 2u) / baud;
    UART_IBRD = div_x64 >> 6;
    UART_FBRD = div_x64 & 0x3Fu;
}

void usb_cn2_init(void) {
    RESET_RESET &= ~RESET_UART1;
    while (!(RESET_DONE & RESET_UART1)) { }

    GPIO_CTRL(CN2_TX_GPIO) = GPIO_FUNC_UART1_AUX;
    GPIO_CTRL(CN2_RX_GPIO) = GPIO_FUNC_UART1_AUX;

    /* Explicit assignments clear stale pulls/ISO state. */
    GPIO_PAD(CN2_TX_GPIO) = PAD_SLEW_FAST | PAD_DRIVE(PAD_DRIVE_4MA);
    GPIO_PAD(CN2_RX_GPIO) = PAD_INPUT;

    UART_CR = 0;
    uint32_t mhz = context.get_sysclk_mhz ? context.get_sysclk_mhz() : 150u;
    uart_set_baud(mhz * 1000000u, CN2_BAUD);
    UART_LCR_H = (3u << 5) | (1u << 4); /* 8 data bits, FIFO enabled, 1 stop, no parity */
    UART_ICR = 0x7FFu;
    UART_CR = (1u << 9) | (1u << 8) | 1u; /* RXE | TXE | UARTEN */
}

void usb_cn2_cdc_rx(const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; ++i) {
        if (UART_FR & UART_FR_TXFF) break;
        UART_DR = data[i];
    }
}

void usb_cn2_task(void) {
    /* Re-assert pad state: stock serving setup can touch SEL pad configuration. */
    GPIO_PAD(CN2_TX_GPIO) = PAD_SLEW_FAST | PAD_DRIVE(PAD_DRIVE_4MA);
    GPIO_PAD(CN2_RX_GPIO) = PAD_INPUT;

    uint8_t buf[64];
    uint32_t n = 0;
    while (!(UART_FR & UART_FR_RXFE) && n < sizeof(buf)) {
        buf[n++] = (uint8_t)UART_DR;
    }
    if (n && tud_cdc_n_connected(1)) {
        uint32_t avail = tud_cdc_n_write_available(1);
        if (n > avail) n = avail;
        if (n) {
            tud_cdc_n_write(1, buf, n);
            tud_cdc_n_write_flush(1);
        }
    }
}
