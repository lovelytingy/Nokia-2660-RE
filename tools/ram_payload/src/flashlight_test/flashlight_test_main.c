#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "syscode.h"
#include "cmd_def.h"
#include "usbio.h"
#include "sdio.h"
#if !UMS9117
#include "sfc.h"
#endif
#include "efuse.h"

#define FAT_READ_SYS \
	if (sdio_read_block(sector, buf)) break;
#include "microfat.h"
fatdata_t fatdata_glob;
#include "fatfile.h"

void lcd_appinit(void) {
	struct sys_display *disp = &sys_data.display;
	int w = disp->w1, h = disp->h1;
	disp->w2 = w;
	disp->h2 = h;
}

#define FONT_W 8
#define FONT_H 16
static const uint8_t font_data[] = {
#include "font8x16.h"
};

static void *framebuf_mem = NULL;
static uint16_t *framebuf = NULL;

static void framebuf_alloc(void) {
	struct sys_display *disp = &sys_data.display;
	int w = disp->w2, h = disp->h2;
	size_t size = w * h;
	uint8_t *p;
	framebuf_mem = p = malloc(size * 2 + 31);
	p += -(intptr_t)p & 31;
	framebuf = (void*)p;
}

static void draw_fill_rect(uint16_t *fb, unsigned w, unsigned h, unsigned x0, unsigned y0, unsigned rw, unsigned rh, uint16_t col) {
	unsigned x, y;
	for (y = y0; y < y0 + rh && y < h; y++) {
		for (x = x0; x < x0 + rw && x < w; x++) {
			fb[y * w + x] = col;
		}
	}
}

static void draw_rect(uint16_t *fb, unsigned w, unsigned h, unsigned x0, unsigned y0, unsigned rw, unsigned rh, uint16_t col) {
	unsigned x, y;
	for (x = x0; x < x0 + rw && x < w; x++) {
		if (y0 < h) fb[y0 * w + x] = col;
		if (y0 + rh - 1 < h) fb[(y0 + rh - 1) * w + x] = col;
	}
	for (y = y0; y < y0 + rh && y < h; y++) {
		if (x0 < w) fb[y * w + x0] = col;
		if (x0 + rw - 1 < w) fb[y * w + (x0 + rw - 1)] = col;
	}
}

static void draw_text_xy(uint16_t *fb, unsigned w, unsigned h, unsigned x0, unsigned y0,
                         const char *str, uint16_t fg, uint16_t bg, unsigned scale) {
	unsigned cx = x0;
	while (*str) {
		char ch = *str++;
		if (ch == '\n') {
			y0 += FONT_H * scale + 2;
			cx = x0;
			continue;
		}
		if ((unsigned)(ch - 0x20) >= 95) ch = '?';
		const uint8_t *bm = font_data + (ch - 0x20) * FONT_H;
		for (unsigned y = 0; y < FONT_H; y++) {
			uint8_t row = bm[y];
			for (unsigned x = 0; x < FONT_W; x++) {
				uint16_t color = (row & (0x80 >> x)) ? fg : bg;
				if (color || bg) {
					for (unsigned sy = 0; sy < scale; sy++) {
						for (unsigned sx = 0; sx < scale; sx++) {
							unsigned px = cx + x * scale + sx;
							unsigned py = y0 + y * scale + sy;
							if (px < w && py < h) {
								fb[py * w + px] = color;
							}
						}
					}
				}
			}
		}
		cx += FONT_W * scale;
	}
}

// =============================================================================
// HARDWARE PERIPHERAL DRIVERS: SC2720 PMIC FLASHLIGHT / TORCH LED
// =============================================================================

uint32_t adi_read(uint32_t addr);
void adi_write(uint32_t addr, uint32_t val);
void adi_write_or(uint32_t addr, uint32_t val);
int eic_read(void);
void eic_mask_or(unsigned val);

// Read dedicated Power/End button from SC2720 PMIC (0x40608280, EIC Pin 1)
// MUST strictly isolate bit 1 with (& 1) so EIC Pin 9 (Key 8) is never mistaken for Power!
static inline int read_power_button(void) {
	return ((eic_read() >> 1) & 1) ^ 1;
}

// SC2720 PMIC Rear Torch / Flashlight LED Register
// Discovered in kernel.bin at 0xbdf4d8 (FLASHLIGHT_Ctrl)
// Bit 15 (0x8000) = LED Power Gate Enable
// Bits [3:0]      = Drive Current / Brightness Level (0 = Dim, 15 = Full Brightness)
#define PMIC_TORCH_REG 0x40608df4

static int g_torch_on = 0;

static void flashlight_set(int on, int level) {
	uint32_t cur = adi_read(PMIC_TORCH_REG);
	if (on) {
		level &= 0x0f;
		// Preserve other bits, set bit 15 (enable), apply intensity level
		adi_write(PMIC_TORCH_REG, (cur & ~0x800f) | 0x8000 | level);
		g_torch_on = 1;
	} else {
		// Clear bit 15 to power off the LED driver
		adi_write(PMIC_TORCH_REG, cur & ~0x8000);
		g_torch_on = 0;
	}
}

static void flashlight_pulse(int duration_ms, int level) {
	flashlight_set(1, level);
	sys_wait_ms(duration_ms);
	flashlight_set(0, level);
}

static void flashlight_morse_sos(int level) {
	// S: 3 short pulses (100ms on, 100ms off)
	for (int i = 0; i < 3; i++) {
		flashlight_pulse(100, level);
		sys_wait_ms(100);
	}
	sys_wait_ms(250);
	// O: 3 long pulses (300ms on, 100ms off)
	for (int i = 0; i < 3; i++) {
		flashlight_pulse(300, level);
		sys_wait_ms(100);
	}
	sys_wait_ms(250);
	// S: 3 short pulses (100ms on, 100ms off)
	for (int i = 0; i < 3; i++) {
		flashlight_pulse(100, level);
		sys_wait_ms(100);
	}
}

static void flashlight_strobe(int count, int delay_ms, int level) {
	for (int i = 0; i < count; i++) {
		flashlight_pulse(delay_ms, level);
		sys_wait_ms(delay_ms);
	}
}

// =============================================================================
// LCD UI DASHBOARD
// =============================================================================
static void draw_flashlight_dashboard(uint16_t *fb, unsigned w, unsigned h, unsigned frame,
                                      const char *status_msg, int torch_on, int level) {
	uint16_t bg_color = 0x0843; // Deep slate navy
	uint16_t flash_color = torch_on ? 0xFFE0 : ((frame & 16) ? 0x07E0 : 0x001F);

	if (w <= 160) {
		// Outer Cover Screen (128x160 GC9104)
		draw_fill_rect(fb, w, h, 0, 0, w, h, bg_color);
		draw_fill_rect(fb, w, h, 0, 0, w, 20, 0x0010);
		draw_text_xy(fb, w, h, 4, 2, "FLASHLIGHT (Cover)", 0xFFE0, 0x0010, 1);

		draw_fill_rect(fb, w, h, 4, 24, w - 8, 48, 0x0000);
		draw_rect(fb, w, h, 4, 24, w - 8, 48, torch_on ? 0xFFE0 : 0x07E0);
		draw_text_xy(fb, w, h, 8, 28, status_msg, torch_on ? 0xFFE0 : 0xFFFF, 0x0000, 1);
		char buf[32];
		snprintf(buf, sizeof(buf), "Brightness: %d/15", level);
		draw_text_xy(fb, w, h, 8, 48, buf, 0xCE79, 0x0000, 1);

		draw_text_xy(fb, w, h, 6, 80, "LED: SC2720 PMIC", 0xFFFF, bg_color, 1);
		draw_text_xy(fb, w, h, 6, 98, "ADI: 0x40608df4", 0x07FF, bg_color, 1);
		draw_text_xy(fb, w, h, 6, 120, "Hold END to exit", 0x8410, bg_color, 1);

		draw_fill_rect(fb, w, h, 0, h - 16, w, 16, 0x0010);
		draw_text_xy(fb, w, h, 6, h - 14, "BARE-METAL DRAM", 0x07E0, 0x0010, 1);
		return;
	}

	// Inner Main Screen (240x320 GC9307)
	draw_fill_rect(fb, w, h, 0, 0, w, h, bg_color);

	// Header banner
	draw_fill_rect(fb, w, h, 0, 0, w, 26, 0x0010);
	draw_text_xy(fb, w, h, (w - 24 * 8) / 2, 5, "NOKIA 2660 REAR TORCH", 0xFFE0, 0x0010, 1);

	// Subtitle
	draw_text_xy(fb, w, h, 14, 30, "TA-1480 | SC2720 Flashlight Driver", 0xCE79, bg_color, 1);

	// Card 1: Hardware Specs Box (48..84)
	draw_fill_rect(fb, w, h, 10, 48, w - 20, 36, 0x0000);
	draw_rect(fb, w, h, 10, 48, w - 20, 36, torch_on ? 0xFFE0 : 0x03E0);
	draw_text_xy(fb, w, h, 16, 52, "PMIC Reg: 0x40608df4 (ADI Bus)", 0x07FF, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 68, "Control: Bit 15 En | Bits [3:0] Lvl", 0xFFFF, 0x0000, 1);

	// Card 2: Interactive Active Status Card (88..162)
	draw_fill_rect(fb, w, h, 10, 88, w - 20, 74, 0x0000);
	draw_rect(fb, w, h, 10, 88, w - 20, 74, flash_color);
	draw_rect(fb, w, h, 12, 90, w - 24, 70, flash_color);

	draw_text_xy(fb, w, h, 18, 96, "TORCH STATUS:", 0x8410, 0x0000, 1);
	draw_text_xy(fb, w, h, 18, 114, status_msg, torch_on ? 0xFFE0 : 0x07E0, 0x0000, 1);

	// Brightness Visual Bar
	char vbuf[32];
	snprintf(vbuf, sizeof(vbuf), "Brightness: [%2d/15] ", level);
	draw_text_xy(fb, w, h, 18, 136, vbuf, 0xCE79, 0x0000, 1);
	// Draw horizontal progress bar
	unsigned bar_x = 18 + 18 * 8;
	unsigned bar_y = 138;
	unsigned bar_w = 40;
	draw_rect(fb, w, h, bar_x, bar_y, bar_w, 12, 0xFFFF);
	unsigned fill_w = (level * (bar_w - 4)) / 15;
	if (fill_w > 0) {
		draw_fill_rect(fb, w, h, bar_x + 2, bar_y + 2, fill_w, 8, 0xFFE0);
	}

	// Card 3: Keypad Control Guide (168..288)
	draw_fill_rect(fb, w, h, 10, 168, w - 20, 120, 0x0000);
	draw_rect(fb, w, h, 10, 168, w - 20, 120, 0x001F);

	draw_text_xy(fb, w, h, 16, 174, "KEYPAD CONTROLS:", 0xFD20, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 192, "[1] Torch Toggle [2] Camera Flash", 0xFFFF, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 208, "[3] Pulse 200ms  [4] Pulse 500ms", 0xFFFF, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 224, "[5] Pulse 1.0s   [6] Fast Strobe", 0x07E0, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 240, "[7] Slow Strobe  [8] Strobe Tick", 0xFFE0, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 256, "[9] SOS Beacon   [0] Full Torch", 0x07FF, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 272, "[Vol+/-] Bright  [*] Turn OFF", 0xCE79, 0x0000, 1);

	// Footer bar
	draw_fill_rect(fb, w, h, 0, h - 22, w, 22, 0x0010);
	draw_text_xy(fb, w, h, 12, h - 18, "Hold Power/End to exit", 0x8410, 0x0010, 1);
	draw_text_xy(fb, w, h, w - 80, h - 18, "[READY]", 0x07E0, 0x0010, 1);
}

// =============================================================================
// FLASHLIGHT APP MAIN LOOP
// =============================================================================
void test_flashlight(void) {
	struct sys_display *disp = &sys_data.display;
	unsigned w = disp->w2, h = disp->h2;
	framebuf_alloc();
	sys_framebuffer(framebuf);
	sys_start();

	// Initialize Flashlight to OFF state
	int brightness = 15; // default full brightness
	flashlight_set(0, brightness);

	printf("\n======================================================\n");
	printf("  NOKIA 2660 FLIP (TA-1480) - REAR FLASHLIGHT / TORCH\n");
	printf("  >>> SC2720 PMIC LED CONTROLLER REG: 0x40608df4 <<<\n");
	printf("======================================================\n");
	printf("  LCD: %ux%u | Volatile DRAM execution active\n", w, h);
	printf("  [1] Torch Toggle (Continuous ON/OFF)\n");
	printf("  [2] Short Camera Flash (50 ms)\n");
	printf("  [3] Pulse 200 ms        [4] Pulse 500 ms\n");
	printf("  [5] Long Beam 1.0 s     [6] Fast Strobe (10x)\n");
	printf("  [7] Slow Strobe (5x)    [8] Single Strobe Tick (25 ms)\n");
	printf("  [9] Emergency SOS Morse Code Optical Beacon\n");
	printf("  [0] Full Torch ON (Level 15)\n");
	printf("  [*] Turn OFF Flashlight\n");
	printf("  [Vol+/Vol-] Adjust Brightness Level (0..15)\n");
	printf("  [Hold END key] Exit payload cleanly\n\n");

	char status_msg[48] = "Torch: OFF (Ready)";
	unsigned frame = 0;
	int last_pb = read_power_button();
	int pb_held_frames = 0;

	// Initial greeting flash (50ms)
	flashlight_pulse(50, 10);

	while (frame < 7200) {
		// 1. Process keypad events
		int key;
		int type;
		while ((type = sys_event(&key)) != EVENT_END) {
			if (type == EVENT_KEYDOWN) {
				int code = key & 0x7fff;
				switch (code) {
				case 0x31: // '1' -> Torch ON / OFF Toggle
					g_torch_on = !g_torch_on;
					flashlight_set(g_torch_on, brightness);
					if (g_torch_on) {
						snprintf(status_msg, sizeof(status_msg), "Torch: ON (Lvl %d)", brightness);
						printf("  [Torch] Flashlight continuously ON at level %d\n", brightness);
					} else {
						strncpy(status_msg, "Torch: OFF", sizeof(status_msg) - 1);
						printf("  [Torch] Flashlight OFF\n");
					}
					break;

				case 0x32: // '2' -> Camera Flash (50 ms)
					strncpy(status_msg, "Camera Flash (50ms)", sizeof(status_msg) - 1);
					draw_flashlight_dashboard(framebuf, w, h, frame, status_msg, 1, brightness);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Torch] Triggering camera flash pulse (50 ms)...\n");
					flashlight_pulse(50, brightness);
					strncpy(status_msg, "Torch: OFF", sizeof(status_msg) - 1);
					break;

				case 0x33: // '3' -> Pulse 200 ms
					strncpy(status_msg, "Pulse (200ms)", sizeof(status_msg) - 1);
					draw_flashlight_dashboard(framebuf, w, h, frame, status_msg, 1, brightness);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Torch] Triggering 200 ms pulse...\n");
					flashlight_pulse(200, brightness);
					strncpy(status_msg, "Torch: OFF", sizeof(status_msg) - 1);
					break;

				case 0x34: // '4' -> Pulse 500 ms
					strncpy(status_msg, "Pulse (500ms)", sizeof(status_msg) - 1);
					draw_flashlight_dashboard(framebuf, w, h, frame, status_msg, 1, brightness);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Torch] Triggering 500 ms pulse...\n");
					flashlight_pulse(500, brightness);
					strncpy(status_msg, "Torch: OFF", sizeof(status_msg) - 1);
					break;

				case 0x35: // '5' -> Pulse 1.0 s
					strncpy(status_msg, "Beam (1.0s)", sizeof(status_msg) - 1);
					draw_flashlight_dashboard(framebuf, w, h, frame, status_msg, 1, brightness);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Torch] Triggering 1.0s pulse...\n");
					flashlight_pulse(1000, brightness);
					strncpy(status_msg, "Torch: OFF", sizeof(status_msg) - 1);
					break;

				case 0x36: // '6' -> Fast Strobe (10x 40ms)
					strncpy(status_msg, "Fast Strobe (10x)", sizeof(status_msg) - 1);
					draw_flashlight_dashboard(framebuf, w, h, frame, status_msg, 1, brightness);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Torch] Executing fast strobe sequence (10 flashes)...\n");
					flashlight_strobe(10, 40, brightness);
					strncpy(status_msg, "Torch: OFF", sizeof(status_msg) - 1);
					break;

				case 0x37: // '7' -> Slow Strobe (5x 120ms)
					strncpy(status_msg, "Slow Strobe (5x)", sizeof(status_msg) - 1);
					draw_flashlight_dashboard(framebuf, w, h, frame, status_msg, 1, brightness);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Torch] Executing slow strobe sequence (5 flashes)...\n");
					flashlight_strobe(5, 120, brightness);
					strncpy(status_msg, "Torch: OFF", sizeof(status_msg) - 1);
					break;

				case 0x38: // '8' -> Single Strobe Tick (25 ms)
					strncpy(status_msg, "Strobe Tick (25ms)", sizeof(status_msg) - 1);
					draw_flashlight_dashboard(framebuf, w, h, frame, status_msg, 1, brightness);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Torch] Triggering fast 25 ms strobe tick...\n");
					flashlight_pulse(25, brightness);
					strncpy(status_msg, "Torch: OFF", sizeof(status_msg) - 1);
					break;

				case 0x39: // '9' -> SOS Morse Code Optical Beacon
					strncpy(status_msg, "SOS Beacon (... --- ...)", sizeof(status_msg) - 1);
					draw_flashlight_dashboard(framebuf, w, h, frame, status_msg, 1, brightness);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Torch] >>> Broadcasting Emergency SOS Morse Code Optical Beacon! <<<\n");
					flashlight_morse_sos(brightness);
					strncpy(status_msg, "Torch: OFF", sizeof(status_msg) - 1);
					break;

				case 0x30: // '0' -> Full Torch ON (Level 15)
					brightness = 15;
					flashlight_set(1, brightness);
					strncpy(status_msg, "Torch: FULL ON (Lvl 15)", sizeof(status_msg) - 1);
					printf("  [Torch] Flashlight locked ON at MAXIMUM brightness (Level 15)\n");
					break;

				case 0x24: // Volume Up -> Increase brightness
					if (brightness < 15) brightness++;
					flashlight_pulse(20, brightness); // feedback flash
					if (g_torch_on) flashlight_set(1, brightness);
					snprintf(status_msg, sizeof(status_msg), "Brightness: %d/15", brightness);
					printf("  [Config] Brightness level set to %d/15\n", brightness);
					break;

				case 0x25: // Volume Down -> Decrease brightness
					if (brightness > 0) brightness--;
					if (brightness > 0) flashlight_pulse(20, brightness); // feedback flash
					if (g_torch_on) flashlight_set(1, brightness);
					snprintf(status_msg, sizeof(status_msg), "Brightness: %d/15", brightness);
					printf("  [Config] Brightness level set to %d/15\n", brightness);
					break;

				case 0x2a: // '*' -> Turn OFF Torch
					flashlight_set(0, brightness);
					strncpy(status_msg, "Torch: OFF", sizeof(status_msg) - 1);
					printf("  [Torch] Flashlight turned OFF.\n");
					break;

				default:
					break;
				}
			} else if (type == EVENT_QUIT) {
				goto end_loop;
			}
		}

		// 2. Poll PMIC Power Button
		int cur_pb = read_power_button();
		if (cur_pb != last_pb) {
			last_pb = cur_pb;
		}

		if (last_pb) {
			pb_held_frames++;
			if (pb_held_frames > 40) {
				printf("\n[+] Power/End button held. Exiting Flashlight payload cleanly!\n");
				break;
			}
		} else {
			pb_held_frames = 0;
		}

		// 3. Render frame
		draw_flashlight_dashboard(framebuf, w, h, frame, status_msg, g_torch_on, brightness);
		sys_start_refresh();
		sys_wait_refresh();
		frame++;
		sys_wait_ms(16);
	}

end_loop:
	// Turn off flashlight on clean exit
	flashlight_set(0, 0);
	printf("\n[+] Flashlight test session finished cleanly.\n");
	if (framebuf_mem) free(framebuf_mem);
}

// =============================================================================
// CLI ENTRY POINT
// =============================================================================
static void usbio_bench(int arg, unsigned len, unsigned n) {
	void *mem = malloc(len + 32);
	uint8_t *buf = (uint8_t*)(((intptr_t)mem + 31) & ~31);
	uint32_t t0, t1; unsigned i;

	{
		union { uint16_t u16[5]; uint32_t u32[2]; } buf;
		unsigned tmp = CMD_USBBENCH | arg << 8;
		buf.u16[0] = tmp; tmp += CHECKSUM_INIT;
		buf.u16[1] = len; tmp += len;
		buf.u32[1] = n; tmp += (uint16_t)n + (n >> 16);
		buf.u16[4] = tmp + (tmp >> 16);
		usb_write(&buf, 10); // sizeof(buf) == 12
	}

	memset(buf, 0, len);
	t0 = sys_timer_ms();
	if (arg == 0)
	for (i = 0; i < n; i += len)
		usb_read(buf, len, USB_WAIT);
	else
	for (i = 0; i < n; i += len)
		usb_write(buf, len);
	t1 = sys_timer_ms();
	free(mem);
	t1 -= t0;
	printf("usbio raw %s: %u bytes in %u ms\n",
			arg ? "write" : "read", i, t1);
}

static void sdio_bench(uint32_t pos, uint32_t size) {
	void *mem = malloc(512 + 32);
	uint8_t *buf = (uint8_t*)(((intptr_t)mem + 31) & ~31);
	unsigned i, n = size >> 9;
	uint32_t t0, t1;

	pos >>= 9;
	t0 = sys_timer_ms();
	for (i = 0; i < n; i++)
		if (sdio_read_block(pos + i, buf)) break;
	t1 = sys_timer_ms();
	free(mem);
	t1 -= t0;
	printf("sdio raw read: %u bytes in %u ms\n", i << 9, t1);
}

static int boot_cable_check(void) {
#if UMS9117
	uint32_t tmp = MEM4(0x402e002c);
	const char *s = "nand, sdio";
	if ((tmp & 3) == 1) s = "sdio";
	else if ((tmp & 3) == 2) s = "spi nand, sdio";
	if (!(tmp & 4)) s = "usb (no wait)";
	printf("boot mode: %s\n", s);
	return ~tmp & 0x10;
#else
	return !(MEM4(0x205000e0) & 2);
#endif
}

static void test_cpuid(void) {
	uint32_t id0, id1;
	__asm__ __volatile__(
#ifdef __thumb__
		".p2align 2\nbx pc\n.p2align 2\n.code 32\n"
#endif
		"mrc p15, #0, %0, c0, c0, #0\n" // MIDR
		"mrc p15, #0, %1, c0, c0, #1\n" // CTR
#ifdef __thumb__
		"blx 1f\n.code 16\n1:\n"
#endif
		: "=r"(id0), "=r"(id1) :: "lr");
	printf("ARM: id = 0x%08x, cache = 0x%08x\n", id0, id1);
}

static int sdtest_init(int target) {
	static int init_done = 0;
	do {
		if (!(init_done & 1)) {
			sdio_init();
			init_done |= 1;
		}
		if (!(init_done & 2)) {
			if (sdcard_init()) break;
			SDIO_VERBOSITY(0);
			init_done |= 2;
		}
		if (!(init_done & 4)) {
			fatdata_glob.buf = (uint8_t*)CHIPRAM_ADDR + 0x9000;
			if (fat_init(&fatdata_glob, 0)) break;
			init_done |= 4;
		}
	} while (0);
	return ~init_done & target;
}

static void test_eic(unsigned mask) {
	unsigned i, val, prev = 0, start = mask, cond;
	eic_mask_or(mask);
	sys_wait_ms(2);
	for (;; start = 0, prev = val) {
		val = eic_read() & mask;
		cond = start | (val ^ prev);
		for (i = 0; i < 16; i++)
			if (cond >> i & 1)
				printf("eic[%u] = %u%s\n", i,
						val >> i & 1, start ? " (start)" : "");
	}
}

static void test_efuse(void) {
	unsigned i, n;
	efuse_init();
	n = EFUSE_MAX;
	for (i = 0; i < n / 2; i++) {
		if (!(i & 3)) printf("0x%02x:", i);
		printf(" %08x", efuse_read(i, 1));
		if ((i & 3) == 3) printf("\n");
	}
	efuse_off();
}

int main(int argc, char **argv) {
	int i;

	if (boot_cable_check())
		printf("boot cable detected\n");

	if (1) {
		printf("argc = %i\n", argc);
		for (i = 0; i < argc; i++)
			printf("[%i] = %s\n", i, argv[i]);
	}

	if (argc < 2) {
		static const char * const tab[] = {
			"fptest", "display", NULL };
		argv = (char**)tab;
		argc = sizeof(tab) / sizeof(*tab) - 1;
	}

	while (argc > 1) {
		if (!strcmp(argv[1], "display")) {
			test_flashlight();
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "cpuid")) {
			test_cpuid();
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "usb")) {
			usbio_bench(0, 1024, 1 << 20);
			usbio_bench(1, 1024, 1 << 20);
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "sdio")) {
			if (!sdtest_init(3)) {
				sdio_bench(1 << 20, 1 << 20);
			}
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "eic")) {
			test_eic(strtol(argv[2], NULL, 0));
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "efuse")) {
			test_efuse();
			argc -= 1; argv += 1;
		} else {
			printf("unknown command (\"%s\")\n", argv[1]);
			return 1;
		}
	}
	return 0;
}

void keytrn_init(void) {
	uint8_t keymap[64];
	int i, flags = sys_getkeymap(keymap);
	(void)flags;

#define FILL_KEYTRN(j) \
	for (i = 0; i < 64; i++) { \
		sys_data.keytrn[j][i] = keymap[i] | j << 15; \
	}

	FILL_KEYTRN(0)
	FILL_KEYTRN(1)
#undef FILL_KEYTRN
}
