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
// HARDWARE PERIPHERAL DRIVERS: SC2720 PMIC VIBRATOR MOTOR (0x40608e00)
// =============================================================================

uint32_t adi_read(uint32_t addr);
void adi_write(uint32_t addr, uint32_t val);
void adi_write_or(uint32_t addr, uint32_t val);
int eic_read(void);
void eic_mask_or(unsigned val);

static inline int read_power_button(void) {
	return ((eic_read() >> 1) & 1) ^ 1;
}

// SC2720 PMIC LDO_VIBR Control Register (0x40608e00)
// Disassembled directly from official MOCOR kernel.bin @ 0x80be704c (analog_phy.c):
//   "Vibrator vlaue  = %d V,POWER_ON!!!!!!!!!!!!!!!"
//   "Vibrator vlaue  = %d,POWER_DOWN!!!!!!!!!!!!!!!"
// Register Bit Layout:
//   Bits [2:0] : LDO output voltage level (0..7 -> 2.8V .. 3.5V)
//   Bit  [8]   : LDO_VIBR_PD (0 = Power ON, 1 = Power Down / Disable)
//   Bit  [9]   : SLP_LDOVIBR_PD_EN (0 = Active, 1 = Sleep Power Down)
#define PMIC_LDO_VIBR_REG 0x40608e00

static void pmic_vib_set(int enable, int level) {
	if (enable) {
		if (level < 0) level = 0;
		if (level > 7) level = 7;
		uint32_t val = adi_read(PMIC_LDO_VIBR_REG);
		// Set voltage level in bits [2:0] (0 = 2.8V, 7 = 3.5V max rumble)
		val = (val & ~0x7) | (level & 0x7);
		// Clear bit 8 (LDO_VIBR_PD) and bit 9 (SLP_LDOVIBR_PD_EN) -> POWER ON!
		val &= ~0x300;
		adi_write(PMIC_LDO_VIBR_REG, val);
	} else {
		uint32_t val = adi_read(PMIC_LDO_VIBR_REG);
		// Set bit 8 and bit 9 -> POWER DOWN!
		val |= 0x300;
		adi_write(PMIC_LDO_VIBR_REG, val);
	}
}

// Master Vibrator Enable (controls SC2720 PMIC LDO_VIBR)
static void haptics_set(int state, int level) {
	pmic_vib_set(state, level);
}

static void haptics_pulse(int duration_ms, int level) {
	haptics_set(1, level);
	sys_wait_ms(duration_ms);
	haptics_set(0, level);
}

static void haptics_morse_sos(int level) {
	// S: 3 short pulses
	for (int i = 0; i < 3; i++) {
		haptics_pulse(80, level);
		sys_wait_ms(80);
	}
	sys_wait_ms(200);
	// O: 3 long pulses
	for (int i = 0; i < 3; i++) {
		haptics_pulse(240, level);
		sys_wait_ms(80);
	}
	sys_wait_ms(200);
	// S: 3 short pulses
	for (int i = 0; i < 3; i++) {
		haptics_pulse(80, level);
		sys_wait_ms(80);
	}
}

// Heartbeat pattern: lub-dub
static void haptics_heartbeat(int level) {
	for (int b = 0; b < 3; b++) {
		haptics_pulse(60, level);
		sys_wait_ms(90);
		haptics_pulse(100, level);
		sys_wait_ms(500);
	}
}

// =============================================================================
// LCD UI DASHBOARD
// =============================================================================
static void draw_haptics_dashboard(uint16_t *fb, unsigned w, unsigned h, unsigned frame,
                                   const char *status_msg, int motor_on, int level) {
	uint16_t bg_color = 0x0843; // Deep navy
	uint16_t active_color = motor_on ? 0x07E0 : ((frame & 16) ? 0xFD20 : 0x001F);

	if (w <= 160) {
		// Outer Cover Screen (128x160 GC9104)
		draw_fill_rect(fb, w, h, 0, 0, w, h, bg_color);
		draw_fill_rect(fb, w, h, 0, 0, w, 20, 0x0010);
		draw_text_xy(fb, w, h, 4, 2, "HAPTICS (Cover)", 0x07E0, 0x0010, 1);

		draw_fill_rect(fb, w, h, 4, 24, w - 8, 48, 0x0000);
		draw_rect(fb, w, h, 4, 24, w - 8, 48, motor_on ? 0x07E0 : 0xFFE0);
		draw_text_xy(fb, w, h, 8, 28, status_msg, motor_on ? 0x07E0 : 0xFFFF, 0x0000, 1);
		char buf[32];
		snprintf(buf, sizeof(buf), "Vib Level: %d/7 (%d.%dV)", level, (28 + level) / 10, (28 + level) % 10);
		draw_text_xy(fb, w, h, 8, 48, buf, 0xCE79, 0x0000, 1);

		draw_text_xy(fb, w, h, 6, 78, "BATTERY REQUIRED!", 0xF800, bg_color, 1);
		draw_text_xy(fb, w, h, 6, 94, "PMIC: 0x40608e00", 0x07FF, bg_color, 1);
		draw_text_xy(fb, w, h, 6, 110, "LDO_VIBR (2.8-3.5V)", 0xFFFF, bg_color, 1);
		draw_text_xy(fb, w, h, 6, 126, "Hold END to exit", 0x8410, bg_color, 1);

		draw_fill_rect(fb, w, h, 0, h - 16, w, 16, 0x0010);
		draw_text_xy(fb, w, h, 6, h - 14, "BARE-METAL DRAM", 0x07E0, 0x0010, 1);
		return;
	}

	// Inner Main Screen (240x320 GC9307)
	draw_fill_rect(fb, w, h, 0, 0, w, h, bg_color);

	// Header banner
	draw_fill_rect(fb, w, h, 0, 0, w, 26, 0x0010);
	draw_text_xy(fb, w, h, (w - 24 * 8) / 2, 5, "NOKIA 2660 HAPTICS LAB", 0x07E0, 0x0010, 1);

	// Subtitle
	draw_text_xy(fb, w, h, 14, 30, "TA-1480 | SC2720 PMIC LDO_VIBR (0x40608e00)", 0xCE79, bg_color, 1);

	// Card 1: Battery Warning Banner (46..76)
	draw_fill_rect(fb, w, h, 10, 46, w - 20, 30, 0x3000);
	draw_rect(fb, w, h, 10, 46, w - 20, 30, 0xF800);
	draw_text_xy(fb, w, h, 14, 50, "! BATTERY MUST BE INSERTED !", 0xF800, 0x3000, 1);
	draw_text_xy(fb, w, h, 14, 64, "ERM motor is powered from VBAT contacts (~3.8V)", 0xFFFF, 0x3000, 1);

	// Card 2: Interactive Active Status Card (80..154)
	draw_fill_rect(fb, w, h, 10, 80, w - 20, 74, 0x0000);
	draw_rect(fb, w, h, 10, 80, w - 20, 74, active_color);
	draw_rect(fb, w, h, 12, 82, w - 24, 70, active_color);

	draw_text_xy(fb, w, h, 18, 88, "MOTOR STATUS:", 0x8410, 0x0000, 1);
	draw_text_xy(fb, w, h, 18, 106, status_msg, motor_on ? 0x07E0 : 0xFFE0, 0x0000, 1);

	// Intensity Progress Bar (0..7)
	char vbuf[32];
	snprintf(vbuf, sizeof(vbuf), "Intensity: [%d/7] %d.%dV", level, (28 + level) / 10, (28 + level) % 10);
	draw_text_xy(fb, w, h, 18, 128, vbuf, 0xCE79, 0x0000, 1);
	unsigned bar_x = 18 + 19 * 8;
	unsigned bar_y = 130;
	unsigned bar_w = 48;
	draw_rect(fb, w, h, bar_x, bar_y, bar_w, 12, 0xFFFF);
	unsigned fill_w = (level * (bar_w - 4)) / 7;
	if (fill_w > 0) {
		draw_fill_rect(fb, w, h, bar_x + 2, bar_y + 2, fill_w, 8, 0x07E0);
	}

	// Card 3: Keypad Control Guide (158..288)
	draw_fill_rect(fb, w, h, 10, 158, w - 20, 126, 0x0000);
	draw_rect(fb, w, h, 10, 158, w - 20, 126, 0x001F);

	draw_text_xy(fb, w, h, 16, 164, "KEYPAD CONTROLS:", 0xFD20, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 182, "[1] Click (40ms)  [2] Double Tick", 0xFFFF, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 198, "[3] Buzz (200ms)  [4] Long (400ms)", 0xFFFF, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 214, "[5] Rumble 750ms  [6] Heartbeat", 0x07E0, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 230, "[7] Continuous ON [8] Stop Motor", 0xFFE0, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 246, "[9] SOS Morse     [0] Heavy Blast", 0x07FF, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 262, "[Vol+/-] Lvl 0..7  [*] Stop Motor", 0xCE79, 0x0000, 1);

	// Footer bar
	draw_fill_rect(fb, w, h, 0, h - 22, w, 22, 0x0010);
	draw_text_xy(fb, w, h, 12, h - 18, "Hold Power/End to exit", 0x8410, 0x0010, 1);
	draw_text_xy(fb, w, h, w - 80, h - 18, "[READY]", 0x07E0, 0x0010, 1);
}

// =============================================================================
// HAPTICS APP MAIN LOOP
// =============================================================================
void test_haptics(void) {
	struct sys_display *disp = &sys_data.display;
	unsigned w = disp->w2, h = disp->h2;
	framebuf_alloc();
	sys_framebuffer(framebuf);
	sys_start();

	int level = 7; // Default max rumble (3.5V)
	haptics_set(0, level);

	printf("\n======================================================\n");
	printf("  NOKIA 2660 FLIP (TA-1480) - HAPTICS & VIBRATOR MOTOR\n");
	printf("  >>> SC2720 PMIC LDO_VIBR (0x40608e00) <<<\n");
	printf("======================================================\n");
	printf("  IMPORTANT: Battery MUST be inserted in phone for motor power!\n");
	printf("  LCD: %ux%u | Volatile DRAM execution active\n", w, h);
	printf("  [1] Short Click (40 ms)   [2] Double Tick (60+60 ms)\n");
	printf("  [3] Medium Buzz (200 ms)  [4] Long Buzz (400 ms)\n");
	printf("  [5] Rumble (750 ms)       [6] Heartbeat Pattern\n");
	printf("  [7] Continuous Motor ON   [8] Stop Motor\n");
	printf("  [9] Emergency SOS Morse Code Vibration\n");
	printf("  [0] Heavy Blast (1.0 s)\n");
	printf("  [Vol+/Vol-] Adjust Vibration Intensity (0..7 -> 2.8V..3.5V)\n");
	printf("  [*] Stop All Vibration    [Hold END key] Exit payload\n\n");

	char status_msg[48] = "Vibrator: Idle (Ready)";
	unsigned frame = 0;
	int motor_active = 0;
	int last_pb = read_power_button();
	int pb_held_frames = 0;

	// Initial greeting tick
	haptics_pulse(40, level);

	while (frame < 7200) {
		// 1. Process keypad events
		int key;
		int type;
		while ((type = sys_event(&key)) != EVENT_END) {
			if (type == EVENT_KEYDOWN) {
				int code = key & 0x7fff;
				switch (code) {
				case 0x31: // '1' -> Short Click (40 ms)
					strncpy(status_msg, "Haptic Click (40ms)", sizeof(status_msg) - 1);
					motor_active = 1;
					draw_haptics_dashboard(framebuf, w, h, frame, status_msg, motor_active, level);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Haptics] Triggering 40 ms tactile click...\n");
					haptics_pulse(40, level);
					motor_active = 0;
					strncpy(status_msg, "Vibrator: Idle", sizeof(status_msg) - 1);
					break;

				case 0x32: // '2' -> Double Tick
					strncpy(status_msg, "Double Tick (60+60ms)", sizeof(status_msg) - 1);
					motor_active = 1;
					draw_haptics_dashboard(framebuf, w, h, frame, status_msg, motor_active, level);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Haptics] Triggering double tick pattern...\n");
					haptics_pulse(60, level);
					sys_wait_ms(60);
					haptics_pulse(60, level);
					motor_active = 0;
					strncpy(status_msg, "Vibrator: Idle", sizeof(status_msg) - 1);
					break;

				case 0x33: // '3' -> Medium Buzz (200 ms)
					strncpy(status_msg, "Buzz (200ms)", sizeof(status_msg) - 1);
					motor_active = 1;
					draw_haptics_dashboard(framebuf, w, h, frame, status_msg, motor_active, level);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Haptics] Triggering 200 ms buzz...\n");
					haptics_pulse(200, level);
					motor_active = 0;
					strncpy(status_msg, "Vibrator: Idle", sizeof(status_msg) - 1);
					break;

				case 0x34: // '4' -> Long Buzz (400 ms)
					strncpy(status_msg, "Long Buzz (400ms)", sizeof(status_msg) - 1);
					motor_active = 1;
					draw_haptics_dashboard(framebuf, w, h, frame, status_msg, motor_active, level);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Haptics] Triggering 400 ms buzz...\n");
					haptics_pulse(400, level);
					motor_active = 0;
					strncpy(status_msg, "Vibrator: Idle", sizeof(status_msg) - 1);
					break;

				case 0x35: // '5' -> Rumble (750 ms)
					strncpy(status_msg, "Rumble (750ms)", sizeof(status_msg) - 1);
					motor_active = 1;
					draw_haptics_dashboard(framebuf, w, h, frame, status_msg, motor_active, level);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Haptics] Triggering 750 ms heavy rumble...\n");
					haptics_pulse(750, level);
					motor_active = 0;
					strncpy(status_msg, "Vibrator: Idle", sizeof(status_msg) - 1);
					break;

				case 0x36: // '6' -> Heartbeat Pattern
					strncpy(status_msg, "Heartbeat Pattern", sizeof(status_msg) - 1);
					motor_active = 1;
					draw_haptics_dashboard(framebuf, w, h, frame, status_msg, motor_active, level);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Haptics] Executing heartbeat vibration sequence...\n");
					haptics_heartbeat(level);
					motor_active = 0;
					strncpy(status_msg, "Vibrator: Idle", sizeof(status_msg) - 1);
					break;

				case 0x37: // '7' -> Continuous Motor ON
					haptics_set(1, level);
					motor_active = 1;
					snprintf(status_msg, sizeof(status_msg), "Motor: CONTINUOUS ON (Lvl %d)", level);
					printf("  [Haptics] Motor continuously ON at level %d (press 8 or * to stop)\n", level);
					break;

				case 0x38: // '8' -> Stop Motor
					haptics_set(0, level);
					motor_active = 0;
					strncpy(status_msg, "Motor: STOPPED", sizeof(status_msg) - 1);
					printf("  [Haptics] Motor stopped.\n");
					break;

				case 0x39: // '9' -> SOS Morse Code Vibration
					strncpy(status_msg, "SOS Morse (... --- ...)", sizeof(status_msg) - 1);
					motor_active = 1;
					draw_haptics_dashboard(framebuf, w, h, frame, status_msg, motor_active, level);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Haptics] >>> Executing Emergency SOS Morse Code Vibration! <<<\n");
					haptics_morse_sos(level);
					motor_active = 0;
					strncpy(status_msg, "Vibrator: Idle", sizeof(status_msg) - 1);
					break;

				case 0x30: // '0' -> Heavy Blast (1.0 s)
					strncpy(status_msg, "Heavy Blast (1.0s)", sizeof(status_msg) - 1);
					motor_active = 1;
					draw_haptics_dashboard(framebuf, w, h, frame, status_msg, motor_active, level);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Haptics] Triggering 1.0 second heavy blast at level %d...\n", level);
					haptics_pulse(1000, level);
					motor_active = 0;
					strncpy(status_msg, "Vibrator: Idle", sizeof(status_msg) - 1);
					break;

				case 0x24: // Volume Up -> Increase intensity
					if (level < 7) level++;
					haptics_pulse(30, level); // feedback tick
					if (motor_active) haptics_set(1, level);
					snprintf(status_msg, sizeof(status_msg), "Intensity: %d/7 (%d.%dV)", level, (28 + level) / 10, (28 + level) % 10);
					printf("  [Config] Haptic intensity set to %d/7 (%d.%dV)\n", level, (28 + level) / 10, (28 + level) % 10);
					break;

				case 0x25: // Volume Down -> Decrease intensity
					if (level > 0) level--;
					if (level > 0) haptics_pulse(30, level); // feedback tick
					if (motor_active) haptics_set(1, level);
					snprintf(status_msg, sizeof(status_msg), "Intensity: %d/7 (%d.%dV)", level, (28 + level) / 10, (28 + level) % 10);
					printf("  [Config] Haptic intensity set to %d/7 (%d.%dV)\n", level, (28 + level) / 10, (28 + level) % 10);
					break;

				case 0x2a: // '*' -> Stop all
					haptics_set(0, level);
					motor_active = 0;
					strncpy(status_msg, "Vibrator: Stopped", sizeof(status_msg) - 1);
					printf("  [Haptics] All motor activity halted.\n");
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
				printf("\n[+] Power/End button held. Exiting Haptics payload cleanly!\n");
				break;
			}
		} else {
			pb_held_frames = 0;
		}

		// 3. Render frame
		draw_haptics_dashboard(framebuf, w, h, frame, status_msg, motor_active, level);
		sys_start_refresh();
		sys_wait_refresh();
		frame++;
		sys_wait_ms(16);
	}

end_loop:
	haptics_set(0, 0);
	printf("\n[+] Haptics test session finished cleanly.\n");
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
		usb_write(&buf, 10);
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
		"mrc p15, #0, %0, c0, c0, #0\n"
		"mrc p15, #0, %1, c0, c0, #1\n"
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
			test_haptics();
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
