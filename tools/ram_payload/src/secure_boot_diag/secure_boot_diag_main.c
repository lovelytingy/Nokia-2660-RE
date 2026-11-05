#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "syscode.h"
#include "cmd_def.h"
#include "usbio.h"
#include "efuse.h"

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

typedef struct {
	uint32_t aon_sec;
	int gate1_aon;
	uint32_t efuse_r0;
	uint32_t efuse_r1;
	uint32_t efuse_r2_antiroll;
	int gate2_efuse;
	uint32_t rotpk[8];
	int rotpk_valid;
	int overall_enforced;
} sec_diag_t;

static sec_diag_t diag_info;

static void read_security_hardware(sec_diag_t *d) {
	memset(d, 0, sizeof(*d));

	// 1. Read Always-On APB Security Register
	d->aon_sec = MEM4(0x402e0568);
	d->gate1_aon = (d->aon_sec & 1);

	// 2. Read eFuse blocks via 0x40240000 controller
	efuse_init();
	d->efuse_r0 = efuse_read(0, 0);
	d->efuse_r1 = efuse_read(1, 0);
	d->efuse_r2_antiroll = efuse_read(2, 1); // Double-bit read for counter

	// Gate 2 check (Row 1 bit 1, exact formula from boot0 FUN_00006622)
	d->gate2_efuse = ((d->efuse_r1 >> 1) & 1);

	// Read Root of Trust Public Key Hash (eFuse Rows 3..10)
	d->rotpk_valid = 0;
	for (int i = 0; i < 8; i++) {
		d->rotpk[i] = efuse_read(3 + i, 0);
		if (d->rotpk[i] != 0) d->rotpk_valid = 1;
	}
	efuse_off();

	// Overall verdict
	d->overall_enforced = (d->gate1_aon && d->gate2_efuse);
}

static void draw_dashboard(uint16_t *fb, unsigned w, unsigned h, unsigned frame, const sec_diag_t *d) {
	// Background
	draw_fill_rect(fb, w, h, 0, 0, w, h, 0x0843);

	// Header banner
	draw_fill_rect(fb, w, h, 0, 0, w, 26, 0x0010);
	draw_text_xy(fb, w, h, (w > 180 ? (w - 18 * 8) / 2 : 8), 5, "NOKIA 2660 SECBOOT", 0x07FF, 0x0010, 1);

	char buf[64];

	// Overall Verdict Card
	uint16_t card_bg = d->overall_enforced ? 0x4000 : 0x0280;
	uint16_t card_border = d->overall_enforced ? 0xF800 : 0x07E0;
	draw_fill_rect(fb, w, h, 8, 32, w - 16, 42, card_bg);
	draw_rect(fb, w, h, 8, 32, w - 16, 42, card_border);

	if (d->overall_enforced) {
		draw_text_xy(fb, w, h, 14, 38, "[!] STATUS: ENFORCED", 0xFFFF, card_bg, 1);
		draw_text_xy(fb, w, h, 14, 56, "NAND: RSA-2048 Active", 0xFBE0, card_bg, 1);
	} else {
		draw_text_xy(fb, w, h, 14, 38, "[*] STATUS: BYPASSED", 0xFFFF, card_bg, 1);
		draw_text_xy(fb, w, h, 14, 56, "NAND: Custom FW Allowed", 0x07FF, card_bg, 1);
	}

	// Gate 1: AON APB Register 0x402e0568
	int y = 80;
	draw_text_xy(fb, w, h, 10, y, "-- HARDWARE GATES --", 0xFFE0, 0x0843, 1);
	y += 18;

	snprintf(buf, sizeof(buf), "Gate 1 (AON): %s", d->gate1_aon ? "ENABLED (1)" : "DISABLED (0)");
	draw_text_xy(fb, w, h, 10, y, buf, d->gate1_aon ? 0x07E0 : 0xF800, 0x0843, 1);
	y += 16;
	snprintf(buf, sizeof(buf), "  Reg 0x402e0568: 0x%08X", (unsigned)d->aon_sec);
	draw_text_xy(fb, w, h, 10, y, buf, 0xCE79, 0x0843, 1);
	y += 20;

	// Gate 2: eFuse Secure Boot Bit
	snprintf(buf, sizeof(buf), "Gate 2 (eFuse): %s", d->gate2_efuse ? "BLOWN (1)" : "CLEAN (0)");
	draw_text_xy(fb, w, h, 10, y, buf, d->gate2_efuse ? 0x07E0 : 0xF800, 0x0843, 1);
	y += 16;
	snprintf(buf, sizeof(buf), "  Row 1 (SecCfg): 0x%08X", (unsigned)d->efuse_r1);
	draw_text_xy(fb, w, h, 10, y, buf, 0xCE79, 0x0843, 1);
	y += 22;

	// Anti-rollback & eFuse inventory
	draw_text_xy(fb, w, h, 10, y, "-- ANTI-ROLLBACK & EFUSE --", 0xFFE0, 0x0843, 1);
	y += 18;
	snprintf(buf, sizeof(buf), "AntiRoll (Row 2): %u (0x%X)", (unsigned)d->efuse_r2_antiroll, (unsigned)d->efuse_r2_antiroll);
	draw_text_xy(fb, w, h, 10, y, buf, 0x07FF, 0x0843, 1);
	y += 16;
	snprintf(buf, sizeof(buf), "ChipID   (Row 0): 0x%08X", (unsigned)d->efuse_r0);
	draw_text_xy(fb, w, h, 10, y, buf, 0xCE79, 0x0843, 1);
	y += 20;

	// Root of Trust Public Key Hash
	draw_text_xy(fb, w, h, 10, y, "-- VENDOR ROOT KEY HASH --", 0xFFE0, 0x0843, 1);
	y += 18;
	if (d->rotpk_valid) {
		snprintf(buf, sizeof(buf), "%08X %08X", (unsigned)d->rotpk[0], (unsigned)d->rotpk[1]);
		draw_text_xy(fb, w, h, 10, y, buf, 0xFFFF, 0x0843, 1);
		y += 16;
		snprintf(buf, sizeof(buf), "%08X %08X...", (unsigned)d->rotpk[2], (unsigned)d->rotpk[3]);
		draw_text_xy(fb, w, h, 10, y, buf, 0xFFFF, 0x0843, 1);
	} else {
		draw_text_xy(fb, w, h, 10, y, "Hash empty (all zeros)", 0x8410, 0x0843, 1);
	}

	// Footer bar
	draw_fill_rect(fb, w, h, 0, h - 22, w, 22, 0x0010);
	snprintf(buf, sizeof(buf), "RAM: 0x80100000 | %us", frame / 30);
	draw_text_xy(fb, w, h, 8, h - 17, buf, 0xFFFF, 0x0010, 1);
	draw_text_xy(fb, w, h, w - 60, h - 17, "[LIVE]", 0x07E0, 0x0010, 1);
}

int eic_read(void);

static inline int read_power_button(void) {
	return ((eic_read() >> 1) & 1) ^ 1;
}

void test_display(void) {
	struct sys_display *disp = &sys_data.display;
	unsigned w = disp->w2, h = disp->h2;
	framebuf_alloc();
	sys_framebuffer(framebuf);
	sys_start();

	read_security_hardware(&diag_info);

	printf("\n=================================================================\n");
	printf("  NOKIA 2660 FLIP (TA-1480) - HARDWARE SECURE BOOT AUDIT\n");
	printf("=================================================================\n");
	printf("  [*] Gate 1: AON Register 0x402e0568 = 0x%08X (Bit 0 = %d: %s)\n",
		(unsigned)diag_info.aon_sec, diag_info.gate1_aon,
		diag_info.gate1_aon ? "ENABLED" : "DISABLED");
	printf("  [*] Gate 2: eFuse Row 1 (SecCfg)   = 0x%08X (Bit 1 = %d: %s)\n",
		(unsigned)diag_info.efuse_r1, diag_info.gate2_efuse,
		diag_info.gate2_efuse ? "BLOWN / ENFORCED" : "CLEAN / BYPASSED");
	printf("  [*] eFuse Row 0 (Chip ID / Calib)  = 0x%08X\n", (unsigned)diag_info.efuse_r0);
	printf("  [*] eFuse Row 2 (Anti-Rollback Ver)= %u (0x%08X)\n",
		(unsigned)diag_info.efuse_r2_antiroll, (unsigned)diag_info.efuse_r2_antiroll);

	if (diag_info.rotpk_valid) {
		printf("  [*] Vendor Root Key SHA-256 Hash:\n      ");
		for (int i = 0; i < 8; i++) {
			printf("%08X ", (unsigned)diag_info.rotpk[i]);
		}
		printf("\n");
	} else {
		printf("  [*] Vendor Root Key Hash is EMPTY (Unburned).\n");
	}

	printf("-----------------------------------------------------------------\n");
	if (diag_info.overall_enforced) {
		printf("  [!] VERDICT: SECURE BOOT IS ENFORCED ON THIS UNIT.\n");
		printf("      boot0/boot1 will enforce RSA-2048 + SHA-256 on NAND images.\n");
		printf("      Custom code execution remains unrestricted via volatile DRAM (FDL1).\n");
	} else {
		printf("  [*] VERDICT: SECURE BOOT IS BYPASSED ON THIS UNIT!\n");
		printf("      boot0/boot1 skip verification; custom code can boot from NAND directly.\n");
	}
	printf("=================================================================\n\n");

	unsigned frame = 0;
	int pb_held_frames = 0;
	int key;

	while (frame < 7200) {
		draw_dashboard(framebuf, w, h, frame, &diag_info);
		sys_start_refresh();
		sys_wait_refresh();

		if ((frame % 60) == 0) {
			printf("  [Diagnostic Alive] Uptime: %u sec | Secure Boot: %s\n",
				frame / 30, diag_info.overall_enforced ? "ENFORCED" : "BYPASSED");
		}

		int type = sys_event(&key);
		if (type == EVENT_QUIT) break;

		if (read_power_button()) {
			pb_held_frames++;
			if (pb_held_frames > 30) {
				printf("\n[+] Power/End key held. Exiting diagnostic payload cleanly.\n");
				break;
			}
		} else {
			pb_held_frames = 0;
		}

		frame++;
		sys_wait_ms(16);
	}

	free(framebuf_mem);
}

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	test_display();
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
