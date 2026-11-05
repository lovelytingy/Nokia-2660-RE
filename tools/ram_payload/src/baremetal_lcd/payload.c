/*
 * Bare-Metal LCD Graphics & Animation Payload for Nokia 2660 Flip (TA-1480 / Unisoc UMS9117)
 * 
 * Loaded into DRAM at 0x80100000 via FDL1 USB download mode.
 * Entry point: 0x80100200.
 * 100% Standalone: zero external libraries or host dependencies.
 * Directly drives the Unisoc UMS9117 LCD Controller (0x20800000) at 240x320 RGB565.
 */

typedef unsigned int       uint32_t;
typedef unsigned short     uint16_t;
typedef unsigned char      uint8_t;
typedef int                int32_t;
typedef short              int16_t;

#define LCDC_BASE       0x20800000
#define LCDC_CTRL       (*(volatile uint32_t*)(LCDC_BASE + 0x00))
#define LCDC_Y_BASE     (*(volatile uint32_t*)(LCDC_BASE + 0x24))
#define LCDC_SIZE_XY    (*(volatile uint32_t*)(LCDC_BASE + 0x2c))
#define LCDC_IRQ_CLR    (*(volatile uint32_t*)(LCDC_BASE + 0x114))
#define LCDC_IRQ_RAW    (*(volatile uint32_t*)(LCDC_BASE + 0x11c))

#define UART1_TX        (*(volatile uint32_t*)0x70100000)
#define TIMER_MS        (*(volatile uint32_t*)0x4023000c)
#define PMIC_TORCH      (*(volatile uint32_t*)0x40608df4)
#define PMIC_VIBR       (*(volatile uint32_t*)0x40608e00)
#define PMIC_EIC        (*(volatile uint32_t*)0x40608280)
#define KPD_STATUS      (*(volatile uint32_t*)0x4025002c)
#define KPD_RAW         (*(volatile uint32_t*)0x40250008)

#define FB_WIDTH        240
#define FB_HEIGHT       320
#define FB_ADDR         0x80200000

// RGB565 Color Definitions
#define C_BLACK         0x0000
#define C_WHITE         0xFFFF
#define C_NOKIA_BLUE    0x016C
#define C_DARK_BG       0x10A2
#define C_RED           0xF800
#define C_GREEN         0x07E0
#define C_BLUE          0x001F
#define C_YELLOW        0xFFE0
#define C_CYAN          0x07FF
#define C_MAGENTA       0xF81F
#define C_ORANGE        0xFD20
#define C_GRAY          0x8410
#define C_LIGHT_GRAY    0xC618

static const uint8_t font8x16[] = {
#include "font8x16.h"
};

static void uart_putc(char c) {
    UART1_TX = (uint32_t)c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void uart_put_num(uint32_t n) {
    char buf[12];
    int i = 0;
    if (n == 0) { uart_putc('0'); return; }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) uart_putc(buf[--i]);
}

static void uart_put_hex(uint32_t n) {
    const char hex[] = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uart_putc(hex[(n >> i) & 0xF]);
    }
}

static void delay_ms(uint32_t ms) {
    uint32_t t0 = TIMER_MS;
    while ((TIMER_MS - t0) < ms);
}

static void fill_rect(uint16_t *fb, int x0, int y0, int w, int h, uint16_t color) {
    for (int y = y0; y < y0 + h; y++) {
        if (y < 0 || y >= FB_HEIGHT) continue;
        int row_offset = y * FB_WIDTH;
        for (int x = x0; x < x0 + w; x++) {
            if (x < 0 || x >= FB_WIDTH) continue;
            fb[row_offset + x] = color;
        }
    }
}

static void draw_char(uint16_t *fb, int x0, int y0, char c, uint16_t fg, uint16_t bg) {
    if (c < 0x20 || c > 0x7E) return;
    const uint8_t *glyph = &font8x16[(c - 0x20) * 16];
    for (int row = 0; row < 16; row++) {
        int y = y0 + row;
        if (y < 0 || y >= FB_HEIGHT) continue;
        uint8_t bits = glyph[row];
        int row_offset = y * FB_WIDTH;
        for (int col = 0; col < 8; col++) {
            int x = x0 + col;
            if (x < 0 || x >= FB_WIDTH) continue;
            fb[row_offset + x] = (bits & (0x80 >> col)) ? fg : bg;
        }
    }
}

static void draw_string(uint16_t *fb, int x, int y, const char *s, uint16_t fg, uint16_t bg) {
    while (*s) {
        draw_char(fb, x, y, *s++, fg, bg);
        x += 8;
    }
}

static void draw_string_num(uint16_t *fb, int x, int y, const char *prefix, uint32_t num, uint16_t fg, uint16_t bg) {
    draw_string(fb, x, y, prefix, fg, bg);
    int offset = 0;
    while (prefix[offset]) offset++;
    char buf[12];
    int i = 0;
    if (num == 0) {
        draw_char(fb, x + offset * 8, y, '0', fg, bg);
        return;
    }
    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    while (i > 0) {
        draw_char(fb, x + offset * 8, y, buf[--i], fg, bg);
        offset++;
    }
}

void main_entry(void) {
    uart_puts("\n======================================================\n");
    uart_puts("  NOKIA 2660 FLIP (TA-1480) - BARE-METAL LCD DEMO\n");
    uart_puts("  >>> LIVE 240x320 QVGA FRAMEBUFFER ENGAGED! <<<\n");
    uart_puts("======================================================\n");

    // 2. Configure SC2720 PMIC: Turn on Flashlight Torch & Haptics Pulse
    PMIC_TORCH = 0x800F; // Turn on torch LED at max brightness
    PMIC_VIBR  = 0x8005; // Pulse vibrator motor
    uart_puts("[*] PMIC Flashlight & Vibrator triggered.\n");

    // 3. Program LCD Controller (0x20800000)
    uint16_t *fb = (uint16_t*)FB_ADDR;
    LCDC_Y_BASE = FB_ADDR >> 2;
    LCDC_SIZE_XY = FB_WIDTH | (FB_HEIGHT << 16);
    LCDC_CTRL |= 1; // Enable LCDC

    uart_puts("[*] LCDC Controller configured at 0x20800000 (y_base=0x80200000).\n");
    uart_puts("[*] Beginning live display rendering loop (30 FPS)...\n\n");

    // 4. Bouncing box animation state
    int bx = 20, by = 160;
    int bdx = 3, bdy = 2;
    const int bsize = 24;
    const uint16_t rainbow[] = { C_RED, C_ORANGE, C_YELLOW, C_GREEN, C_CYAN, C_BLUE, C_MAGENTA };

    uint32_t frame = 0;
    while (frame < 900) { // 30 seconds at 30 FPS
        // Draw Background
        fill_rect(fb, 0, 0, FB_WIDTH, FB_HEIGHT, C_DARK_BG);

        // Header: Nokia Blue Banner
        fill_rect(fb, 0, 0, FB_WIDTH, 42, C_NOKIA_BLUE);
        draw_string(fb, 56, 8, "NOKIA 2660 FLIP", C_WHITE, C_NOKIA_BLUE);
        draw_string(fb, 44, 24, "UMS9117 RE DEMO", C_YELLOW, C_NOKIA_BLUE);

        // System Telemetry Panel
        fill_rect(fb, 8, 48, FB_WIDTH - 16, 56, 0x18E3);
        draw_string(fb, 16, 52, "CPU: ARM Cortex-A7 @ 1.0 GHz", C_WHITE, 0x18E3);
        draw_string(fb, 16, 68, "RAM: Volatile DDR (0x80100000)", C_GREEN, 0x18E3);
        draw_string(fb, 16, 84, "LCD: 240x320 RGB565 via SPI", C_CYAN, 0x18E3);

        // Color Calibration Bars (8 vertical swatches)
        const uint16_t colors[] = { C_WHITE, C_YELLOW, C_CYAN, C_GREEN, C_MAGENTA, C_RED, C_BLUE, C_GRAY };
        int bar_w = (FB_WIDTH - 16) / 8;
        for (int i = 0; i < 8; i++) {
            fill_rect(fb, 8 + i * bar_w, 108, bar_w, 20, colors[i]);
        }

        // Live Keypad Monitor Readout
        uint32_t kpd_raw = KPD_RAW;
        uint32_t kpd_stat = KPD_STATUS;
        fill_rect(fb, 8, 132, FB_WIDTH - 16, 24, 0x2126);
        if (kpd_raw & 1) {
            draw_string(fb, 16, 136, "KEY PRESSED: Code 0x", C_YELLOW, 0x2126);
            const char hex[] = "0123456789ABCDEF";
            draw_char(fb, 176, 136, hex[(kpd_stat >> 4) & 0xF], C_YELLOW, 0x2126);
            draw_char(fb, 184, 136, hex[kpd_stat & 0xF], C_YELLOW, 0x2126);
        } else {
            draw_string(fb, 16, 136, "KEYPAD: Waiting for input...", C_LIGHT_GRAY, 0x2126);
        }

        // Animated Bouncing Rainbow Square
        bx += bdx;
        by += bdy;
        if (bx <= 10 || bx + bsize >= FB_WIDTH - 10) bdx = -bdx;
        if (by <= 160 || by + bsize >= 280) bdy = -bdy;

        uint16_t box_color = rainbow[(frame / 10) % 7];
        fill_rect(fb, bx, by, bsize, bsize, box_color);
        fill_rect(fb, bx + 4, by + 4, bsize - 8, bsize - 8, C_WHITE);

        // Footer: Status & Heartbeat Counter
        fill_rect(fb, 0, 296, FB_WIDTH, 24, 0x0842);
        draw_string_num(fb, 12, 300, "FPS: 30 | Frame: ", frame, C_GREEN, 0x0842);

        // Trigger Virtual Framebuffer Refresh via LCDC
        LCDC_CTRL |= 8; // Bit 3 = Start Refresh
        while (!(LCDC_IRQ_RAW & 1)); // Wait for refresh complete IRQ
        LCDC_IRQ_CLR = 1; // Clear IRQ

        // Serial Heartbeat Console output every 60 frames (2 seconds)
        if ((frame % 60) == 0) {
            uart_puts("  [Heartbeat] Frame: ");
            uart_put_num(frame);
            uart_puts(" | Uptime: ");
            uart_put_num(frame / 30);
            uart_puts("s | LCDC: Refresh OK\n");
        }

        // Exit immediately if user holds PMIC Power/End key (bit 1 active low)
        if ((PMIC_EIC & 2) == 0) {
            uart_puts("\n[+] Power/End key detected! Exiting cleanly.\n");
            break;
        }

        frame++;
        delay_ms(30);
    }

    // 5. Finalize Diagnostics in DRAM Mailbox (0x80700100)
    volatile uint32_t *mb = (volatile uint32_t*)0x80700100;
    mb[0] = 0x14802660; // Nokia TA-1480
    mb[1] = 0xCAFEBABE; // Exec confirmed
    mb[2] = 0xDEADBEEF; // Payload alive
    mb[3] = frame;      // Frames rendered
    mb[4] = 1;          // Status SUCCESS

    uart_puts("\n[+] LCD Demo completed successfully! Handing off back to loader.\n");

    // Turn off Flashlight & Vibrator
    PMIC_TORCH = 0x0000;
    PMIC_VIBR  = 0x0000;
}
