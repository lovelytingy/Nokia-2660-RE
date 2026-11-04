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
// HARDWARE PERIPHERAL DRIVERS: SC2720 PMIC, GPIO & EXTERNAL AUDIO PA
// =============================================================================

uint32_t adi_read(uint32_t addr);
void adi_write(uint32_t addr, uint32_t val);
void adi_write_or(uint32_t addr, uint32_t val);
int eic_read(void);

// Read dedicated Power/End button from SC2720 PMIC (0x40608280, EIC Pin 1)
// MUST strictly isolate bit 1 with (& 1) so EIC Pin 9 (Key 8) is never mistaken for Power!
static inline int read_power_button(void) {
	return ((eic_read() >> 1) & 1) ^ 1;
}

#define GPIO_BASE 0x40280000

// Set GPIO pin state (off=0: DATA, off=4: DMSK, off=8: DIR, off=0x28: INEN)
static int gpio_pin_set(unsigned id, unsigned off, int state) {
	uint32_t addr, tmp, shl = id & 0xf;
	if (id >= 0x80) return -1;
	addr = GPIO_BASE + ((id >> 4) << 7);
	addr += off;
	tmp = MEM4(addr);
	if (state < 0) return tmp >> shl & 1;
	tmp &= ~(1u << shl);
	tmp |= (state & 1) << shl;
	MEM4(addr) = tmp;
	return 0;
}

// External Class-D Audio PA is controlled via APB GPIO 31
#define AUDIO_PA_GPIO 31

// SC2720 PMIC Audio Codec Register Addresses
#define PMIC_ANA_BASE   0x40608700
#define PMIC_CFGA_BASE  0x40608800
#define PMIC_LDO_VIBR   0x40608e00

void sys_wait_clk(uint32_t delay);

// Initialize External PA GPIO pin
static void audio_pa_init_gpio(void) {
	gpio_pin_set(AUDIO_PA_GPIO, 4, 1);    // GPIO_DMSK
	gpio_pin_set(AUDIO_PA_GPIO, 8, 1);    // GPIO_DIR = output
	gpio_pin_set(AUDIO_PA_GPIO, 0x28, 0); // GPIO_INEN = disabled
	gpio_pin_set(AUDIO_PA_GPIO, 0, 0);    // Start LOW (disabled / shutdown)
}

// Enable External PA in Mode 2 (Speaker Mode)
// Re-engineered from MOCOR kernel.bin codec_ext_pa_ctl @ 0x115890:
// Sends 1 pulse (HIGH -> LOW) and then holds HIGH to keep the amplifier active.
static void audio_pa_enable(void) {
	audio_pa_init_gpio();
	// 1-wire pulse: HIGH (5us) -> LOW (5us) -> HIGH (stays active)
	gpio_pin_set(AUDIO_PA_GPIO, 0, 1);
	sys_wait_clk(250);
	gpio_pin_set(AUDIO_PA_GPIO, 0, 0);
	sys_wait_clk(250);
	gpio_pin_set(AUDIO_PA_GPIO, 0, 1);
	sys_wait_clk(250);
}

// Disable External PA (Shutdown Mode: hold LOW > 1ms)
static void audio_pa_disable(void) {
	gpio_pin_set(AUDIO_PA_GPIO, 0, 0);
	sys_wait_ms(2);
}

// Initialize PMIC Audio Codec bias, clocks & Line-Out route
// Re-engineered from official Unisoc MOCOR kernel.bin:
//   - _sprd_codec_sc2720_init (@ 0x80157540): enables PMIC global clock 0x40608c08 bit 4
//   - __sprd_codec2720_ap_en / cfg (@ 0x80135d26): un-gates digital clocks 0x40608838 & 0x4060883c
//   - analog bias & bandgap (@ 0x80157558, 0x801575a0, 0x80157970): 0x40608730, 0x40608734, 0x40608738
//   - __sprd_codec_ap_lout (@ 0x801368d0): un-mutes LOUT on 0x40608724 (bits 13 & 0)
static void pmic_codec_init(int initial_gain) {
	// 1. Force Vibrator Motor LDO strictly OFF (so it never vibrates during audio test)
	adi_write(PMIC_LDO_VIBR, adi_read(PMIC_LDO_VIBR) | 0x300);

	// 2. Un-gate PMIC Master Audio Codec Clock (0x40608c08 bit 4)
	// Without this, all codec registers (0x40608700..0x406088ff) are completely unclocked
	// and read as 0x0000, silently discarding any writes!
	adi_write_or(0x40608c08, 0x10);

	// 3. Enable Codec Digital Clock & Logic Gates (0x40608838 bits [5:2] & 0x4060883c bit 0)
	adi_write_or(0x40608838, 0x3c);
	adi_write_or(0x4060883c, 0x01);

	// 4. Enable Codec Analog Bias / Voltage References (0x40608718 bits [9:4])
	adi_write_or(0x40608718, 0x03f0);

	// 5. Initialize PMIC Analog Audio Reference, Bias, and Bandgap
	// Exact sequence from _sprd_codec_sc2720_init:
	adi_write_or(0x40608730, 0x0a02); // Bits 11, 9, 1
	adi_write_or(0x40608734, 0xc805); // Bits [15:14]=3, [12:11]=1, [3:2]=1, [1:0]=1
	adi_write_or(0x40608738, 0x0534); // Bits [10:7]=0xa, [6:5]=1, bit 4=1, bit 2=1
	adi_write_or(0x4060873c, 0x0061); // Bandgap enable

	// 6. Configure Line-Out Route (LOUT to External PA):
	// 0x40608704: bits [2:0] = 2 (LOUT select)
	adi_write(0x40608704, (adi_read(0x40608704) & ~0x7) | 0x2);

	// 0x40608708: set gain (bits [8:5])
	if (initial_gain < 0) initial_gain = 0;
	if (initial_gain > 15) initial_gain = 15;
	adi_write(0x40608708, (adi_read(0x40608708) & ~0x1e0) | ((initial_gain & 0xf) << 5));

	// 0x40608724: bits 13 (0x2000) and 0 (0x0001) = 1 (LOUT unmuted / active)
	// (Reversed directly from __sprd_codec_ap_lout @ 0x801368d0)
	adi_write_or(0x40608724, 0x2001);

	// Ensure PA starts in clean muted state
	audio_pa_disable();
}

static void pmic_codec_set_gain(int gain) {
	if (gain < 0) gain = 0;
	if (gain > 15) gain = 15;
	uint32_t val = adi_read(0x40608708);
	val = (val & ~0x1e0) | ((gain & 0xf) << 5);
	adi_write(0x40608708, val);
}

// Direct Acoustic PA Tone Generation
// Drives the Awinic Class-D power amplifier directly via APB GPIO 31 at the exact acoustic frequency.
// This physically modulates the speaker H-bridge, producing guaranteed audible square-wave sound
// from the Nokia 2660's loudspeaker!
static void pa_play_acoustic_tone(uint32_t freq_hz, uint32_t duration_ms) {
	if (freq_hz == 0 || duration_ms == 0) {
		if (duration_ms) sys_wait_ms(duration_ms);
		return;
	}

	audio_pa_init_gpio();

	uint32_t half_period_us = 500000 / freq_hz;
	if (half_period_us < 20) half_period_us = 20;
	uint32_t total_cycles = (freq_hz * duration_ms) / 1000;
	if (total_cycles == 0) total_cycles = 1;

	for (uint32_t i = 0; i < total_cycles; i++) {
		gpio_pin_set(AUDIO_PA_GPIO, 0, 1);
		sys_wait_us(half_period_us);
		gpio_pin_set(AUDIO_PA_GPIO, 0, 0);
		sys_wait_us(half_period_us);
	}

	audio_pa_disable();
}

// Drive audio tone through External Speaker PA + PMIC Line-Out DAC
// Modulates PMIC Line-Out analog gain into the active Class-D PA (1-wire Mode 2)
static void speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms, int gain) {
	if (freq_hz == 0 || duration_ms == 0) {
		if (duration_ms) sys_wait_ms(duration_ms);
		return;
	}

	uint32_t half_period_us = 500000 / freq_hz;
	if (half_period_us < 25) half_period_us = 25;
	uint32_t total_cycles = (freq_hz * duration_ms) / 1000;
	if (total_cycles == 0) total_cycles = 1;

	// Energize External PA in Speaker Mode 2
	audio_pa_enable();

	uint32_t reg_08 = adi_read(0x40608708) & ~0x1e0;
	uint32_t val_hi = reg_08 | ((gain & 0xf) << 5);
	uint32_t val_lo = reg_08; // zero gain
	uint32_t reg_24 = adi_read(0x40608724);

	for (uint32_t i = 0; i < total_cycles; i++) {
		// HIGH phase: full gain + unmuted
		adi_write(0x40608708, val_hi);
		adi_write(0x40608724, reg_24 | 0x2001);
		sys_wait_us(half_period_us);

		// LOW phase: zero gain + muted
		adi_write(0x40608708, val_lo);
		adi_write(0x40608724, reg_24 & ~0x2001);
		sys_wait_us(half_period_us);
	}

	// Restore unmuted idle state and power down PA to prevent idle hiss
	adi_write(0x40608708, val_hi);
	adi_write(0x40608724, reg_24 | 0x2001);
	audio_pa_disable();
}

// Acoustic Click / PA Pulse Test
// Rapidly gates the external amplifier to produce audible cone clicks
static void speaker_click_test(int count) {
	for (int i = 0; i < count; i++) {
		audio_pa_enable();
		sys_wait_ms(4);
		audio_pa_disable();
		sys_wait_ms(40);
	}
}

// Loud Speaker Siren / Fast Acoustic Warble (Direct PA)
static void speaker_siren_test(void) {
	for (int rep = 0; rep < 3; rep++) {
		for (uint32_t f = 400; f < 1600; f += 40) {
			pa_play_acoustic_tone(f, 8);
		}
		for (uint32_t f = 1600; f > 400; f -= 40) {
			pa_play_acoustic_tone(f, 8);
		}
	}
}

// =============================================================================
// MELODY DEFINITIONS
// =============================================================================
typedef struct {
	uint16_t freq_hz;
	uint16_t duration_ms;
} note_t;

// The iconic Nokia Tune (Gran Vals by Francisco Tarrega)
static const note_t melody_nokia[] = {
	{ 659, 160 }, // E5
	{ 587, 160 }, // D5
	{ 370, 320 }, // F#4
	{ 415, 320 }, // G#4
	{ 554, 160 }, // C#5
	{ 494, 160 }, // B4
	{ 294, 320 }, // D4
	{ 330, 320 }, // E4
	{ 494, 160 }, // B4
	{ 440, 160 }, // A4
	{ 277, 320 }, // C#4
	{ 330, 320 }, // E4
	{ 440, 640 }, // A4
	{ 0, 0 }
};

// Super Mario Coin Sound
static const note_t melody_mario_coin[] = {
	{ 988, 100 },  // B5
	{ 1319, 450 }, // E6
	{ 0, 0 }
};

// Super Mario Intro Jingle
static const note_t melody_mario_intro[] = {
	{ 659, 140 }, // E5
	{ 659, 140 }, // E5
	{ 0,   140 }, // rest
	{ 659, 140 }, // E5
	{ 0,   140 }, // rest
	{ 523, 140 }, // C5
	{ 659, 200 }, // E5
	{ 784, 350 }, // G5
	{ 0,   200 }, // rest
	{ 392, 350 }, // G4
	{ 0, 0 }
};

static void play_melody(const note_t *notes, int gain) {
	(void)gain;
	while (notes->freq_hz || notes->duration_ms) {
		if (notes->freq_hz) {
			pa_play_acoustic_tone(notes->freq_hz, notes->duration_ms);
		} else {
			sys_wait_ms(notes->duration_ms);
		}
		sys_wait_ms(25); // tiny inter-note gap
		notes++;
	}
	audio_pa_disable();
}

static void play_chirp(int gain) {
	(void)gain;
	for (uint32_t f = 300; f <= 2500; f += 40) {
		pa_play_acoustic_tone(f, 15);
	}
	audio_pa_disable();
}

// Dump PMIC Audio Codec & Global Clock/Power registers to console for diagnostics
static void dump_codec_registers(void) {
	printf("\n--- SC2720 PMIC & AUDIO SUBSYSTEM DUMP ---\n");
	printf("[PMIC GLOBAL CLOCK & POWER]\n");
	printf("  0x40608c00 (MODULE_EN0): 0x%04x\n", (unsigned)adi_read(0x40608c00));
	printf("  0x40608c04 (MODULE_EN1): 0x%04x\n", (unsigned)adi_read(0x40608c04));
	printf("  0x40608c08 (MODULE_CLK): 0x%04x (Audio Codec Clock bit 4 = %u)\n",
	       (unsigned)adi_read(0x40608c08), (unsigned)((adi_read(0x40608c08) >> 4) & 1));
	printf("  0x40608c0c (SOFT_RST0) : 0x%04x\n", (unsigned)adi_read(0x40608c0c));
	printf("  0x40608d34 (LDO_AVDD)  : 0x%04x\n", (unsigned)adi_read(0x40608d34));

	printf("[SC2720 CODEC DIGITAL CLOCK & CFG]\n");
	printf("  0x40608838 (CFG_38)    : 0x%04x (Digital Clock bits[5:2]=0x%x)\n",
	       (unsigned)adi_read(0x40608838), (unsigned)((adi_read(0x40608838) >> 2) & 0xf));
	printf("  0x4060883c (CFG_3C)    : 0x%04x (Clock Gate bit 0 = %u)\n",
	       (unsigned)adi_read(0x4060883c), (unsigned)(adi_read(0x4060883c) & 1));

	printf("[SC2720 CODEC ANALOG FRONT-END & LOUT]\n");
	printf("  0x40608700 (ANA_00)    : 0x%04x\n", (unsigned)adi_read(0x40608700));
	printf("  0x40608704 (ANA_04)    : 0x%04x (Route Select bits[2:0]=%u)\n",
	       (unsigned)adi_read(0x40608704), (unsigned)(adi_read(0x40608704) & 7));
	printf("  0x40608708 (ANA_08)    : 0x%04x (Gain bits[8:5]=0x%x)\n",
	       (unsigned)adi_read(0x40608708), (unsigned)((adi_read(0x40608708) >> 5) & 0xf));
	printf("  0x40608718 (ANA_18)    : 0x%04x (Bias/Power bits[9:4]=0x%x)\n",
	       (unsigned)adi_read(0x40608718), (unsigned)((adi_read(0x40608718) >> 4) & 0x3f));
	printf("  0x40608724 (ANA_24)    : 0x%04x (LOUT En bits[13,0]=0x%x)\n",
	       (unsigned)adi_read(0x40608724), (unsigned)(adi_read(0x40608724) & 0x2001));
	printf("  0x40608730 (ANA_30)    : 0x%04x (Bias Ref bits[11,9,1])\n", (unsigned)adi_read(0x40608730));
	printf("  0x40608734 (ANA_34)    : 0x%04x (Bandgap)\n", (unsigned)adi_read(0x40608734));
	printf("  0x40608738 (ANA_38)    : 0x%04x (Analog Power)\n", (unsigned)adi_read(0x40608738));
	printf("  0x4060873c (ANA_3C)    : 0x%04x (Bandgap En bit0=%u)\n",
	       (unsigned)adi_read(0x4060873c), (unsigned)(adi_read(0x4060873c) & 1));

	printf("[PERIPHERALS & ROUTING]\n");
	printf("  0x40608e00 (LDO_VIBR)  : 0x%04x (Vibrator PD=%u)\n",
	       (unsigned)adi_read(PMIC_LDO_VIBR), (unsigned)((adi_read(PMIC_LDO_VIBR) >> 8) & 1));
	printf("  GPIO 31    (AUDIO_PA)  : DATA=%d, DIR=%d, DMSK=%d\n",
	       gpio_pin_set(AUDIO_PA_GPIO, 0, -1),
	       gpio_pin_set(AUDIO_PA_GPIO, 8, -1),
	       gpio_pin_set(AUDIO_PA_GPIO, 4, -1));
	printf("------------------------------------------\n\n");
}

// =============================================================================
// LCD UI DASHBOARD
// =============================================================================
static void draw_audio_dashboard(uint16_t *fb, unsigned w, unsigned h, unsigned frame,
                                 const char *status_msg, int gain_level, int pa_active) {
	if (w <= 160) {
		// Outer Cover Screen Compact Mode (128x160)
		draw_fill_rect(fb, w, h, 0, 0, w, h, 0x0843);
		draw_fill_rect(fb, w, h, 0, 0, w, 20, 0x0010);
		draw_text_xy(fb, w, h, 4, 2, "SPEAKER AUDIO", 0x07FF, 0x0010, 1);

		draw_fill_rect(fb, w, h, 4, 24, w - 8, 48, 0x0000);
		draw_rect(fb, w, h, 4, 24, w - 8, 48, pa_active ? 0x07E0 : 0xFFE0);
		draw_text_xy(fb, w, h, 8, 28, status_msg, pa_active ? 0x07E0 : 0xFFFF, 0x0000, 1);
		char buf[32];
		snprintf(buf, sizeof(buf), "Gain: %d/15", gain_level);
		draw_text_xy(fb, w, h, 8, 48, buf, 0xCE79, 0x0000, 1);

		draw_text_xy(fb, w, h, 6, 80, "PA: GPIO 31", 0xFFFF, 0x0843, 1);
		draw_text_xy(fb, w, h, 6, 96, "PMIC: 0x40608708", 0x07FF, 0x0843, 1);
		draw_text_xy(fb, w, h, 6, 114, "Hold END to exit", 0x8410, 0x0843, 1);

		draw_fill_rect(fb, w, h, 0, h - 16, w, 16, 0x0010);
		draw_text_xy(fb, w, h, 6, h - 14, "BARE-METAL DRAM", 0x07E0, 0x0010, 1);
		return;
	}

	// Inner Main Screen (240x320)
	// Dark Navy Background
	draw_fill_rect(fb, w, h, 0, 0, w, h, 0x0843);

	// Header banner
	draw_fill_rect(fb, w, h, 0, 0, w, 26, 0x0010);
	draw_text_xy(fb, w, h, (w - 22 * 8) / 2, 5, "NOKIA 2660 FLIP AUDIO", 0x07FF, 0x0010, 1);

	// Subtitle
	draw_text_xy(fb, w, h, 14, 30, "TA-1480 | External Speaker PA & PMIC DAC", 0xCE79, 0x0843, 1);

	// Card 1: Hardware Specs Box (48..84)
	draw_fill_rect(fb, w, h, 10, 48, w - 20, 36, 0x0000);
	draw_rect(fb, w, h, 10, 48, w - 20, 36, 0x03E0);
	draw_text_xy(fb, w, h, 16, 52, "Speaker PA: GPIO 31 (Direct & Mode 2)", 0x07E0, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 68, "Audio Route: Speaker PA & PMIC LOUT", 0xFFFF, 0x0000, 1);

	// Card 2: Interactive Active Sound Card (88..160)
	uint16_t border_col = pa_active ? 0x07E0 : ((frame & 16) ? 0xFD20 : 0x001F);
	draw_fill_rect(fb, w, h, 10, 88, w - 20, 72, 0x0000);
	draw_rect(fb, w, h, 10, 88, w - 20, 72, border_col);
	draw_rect(fb, w, h, 12, 90, w - 24, 68, border_col);

	draw_text_xy(fb, w, h, 18, 96, "CURRENT ACTION:", 0x8410, 0x0000, 1);
	draw_text_xy(fb, w, h, 18, 114, status_msg, pa_active ? 0x07E0 : 0xFFE0, 0x0000, 1);

	// Volume Bar & Value
	char vbuf[32];
	snprintf(vbuf, sizeof(vbuf), "Line-Out Gain: [%d/15]", gain_level);
	draw_text_xy(fb, w, h, 18, 136, vbuf, 0xCE79, 0x0000, 1);

	// Draw volume meter ticks
	unsigned bar_x = 160;
	unsigned bar_y = 138;
	for (int b = 0; b < 15; b++) {
		uint16_t bcol = (b < gain_level) ? (b > 11 ? 0xF800 : (b > 7 ? 0xFFE0 : 0x07E0)) : 0x2104;
		draw_fill_rect(fb, w, h, bar_x + b * 4, bar_y, 3, 10, bcol);
	}

	// Card 3: Keypad Control Matrix Guide (166..286)
	draw_fill_rect(fb, w, h, 10, 166, w - 20, 122, 0x0000);
	draw_rect(fb, w, h, 10, 166, w - 20, 122, 0x001F);

	draw_text_xy(fb, w, h, 16, 172, "KEYPAD CONTROLS:", 0xFD20, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 190, "[1] 440Hz Beep   [2] 880Hz Tone", 0xFFFF, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 206, "[3] 1000Hz Test  [4] 2000Hz High", 0xFFFF, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 222, "[5] Nokia Tune   [6] Mario Coin", 0x07E0, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 238, "[7] Audio Sweep  [8] Cone Click", 0xFFE0, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 254, "[9] Codec Dump   [0] LOUT DAC Test", 0x07FF, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 270, "[*] Mute sound   [#] Mario Theme", 0xCE79, 0x0000, 1);

	// Footer bar
	draw_fill_rect(fb, w, h, 0, h - 22, w, 22, 0x0010);
	draw_text_xy(fb, w, h, 12, h - 18, "Hold Power/End to exit", 0x8410, 0x0010, 1);
	draw_text_xy(fb, w, h, w - 80, h - 18, pa_active ? "[ACTIVE]" : "[IDLE]",
	             pa_active ? 0x07E0 : 0x8410, 0x0010, 1);
}

// =============================================================================
// AUDIO TEST APP MAIN LOOP
// =============================================================================
void test_audio(void) {
	struct sys_display *disp = &sys_data.display;
	unsigned w = disp->w2, h = disp->h2;
	framebuf_alloc();
	sys_framebuffer(framebuf);
	sys_start();

	int gain_level = 12; // Default high volume (0..15)

	// Initialize Audio PA & PMIC audio codec route
	pmic_codec_init(gain_level);

	printf("\n======================================================\n");
	printf("  NOKIA 2660 FLIP (TA-1480) - LOUDSPEAKER AUDIO DRIVER\n");
	printf("  >>> EXTERNAL PA (GPIO 31) & PMIC LINE-OUT (0x40608708) <<<\n");
	printf("======================================================\n");
	printf("  LCD: %ux%u | Volatile DRAM execution active\n", w, h);
	printf("  IMPORTANT: Class-D amplifier draws power from VBAT.\n");
	printf("             Make sure the battery is installed in phone!\n\n");
	printf("  [1] 440 Hz Beep         [2] 880 Hz Tone\n");
	printf("  [3] 1000 Hz Beep        [4] 2000 Hz Tone\n");
	printf("  [5] Classic Nokia Tune  [6] Super Mario Coin\n");
	printf("  [7] Frequency Sweep     [8] Speaker Cone Click Test\n");
	printf("  [9] Codec Reg Dump      [0] 1 kHz Continuous Tone Toggle\n");
	printf("  [Vol+/Vol-] Adjust Line-Out Gain (0..15)\n");
	printf("  [*] Stop Audio / Mute   [Hold END key] Exit payload\n\n");

	unsigned frame = 0;
	int pa_active = 0;
	char status_msg[32] = "READY (Press 1-9 to Play)";

	int last_pb = read_power_button();
	int pb_held_frames = 0;

	// Draw and refresh first frame immediately so screen is active right away
	draw_audio_dashboard(framebuf, w, h, frame, status_msg, gain_level, pa_active);
	sys_start_refresh();
	sys_wait_refresh();

	while (frame < 7200) { // Up to 4 minutes interactive runtime
		// 1. Drain keypad events
		int key;
		int type;
		while ((type = sys_event(&key)) != EVENT_END) {
			if (type == EVENT_KEYDOWN) {
				int code = key & 0x7fff;
				switch (code) {
				case 0x31: // '1' -> 440 Hz Beep (Direct PA Acoustic Tone)
					strncpy(status_msg, "PA Tone: 440 Hz (Beep)", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, gain_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] Playing 440 Hz Concert A tone via PA GPIO 31...\n");
					pa_play_acoustic_tone(440, 350);
					pa_active = 0;
					break;

				case 0x32: // '2' -> 880 Hz Tone
					strncpy(status_msg, "PA Tone: 880 Hz (A5)", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, gain_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] Playing 880 Hz High A tone via PA GPIO 31...\n");
					pa_play_acoustic_tone(880, 350);
					pa_active = 0;
					break;

				case 0x33: // '3' -> 1000 Hz Test Beep
					strncpy(status_msg, "PA Tone: 1000 Hz Test", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, gain_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] Playing 1000 Hz Reference Beep via PA GPIO 31...\n");
					pa_play_acoustic_tone(1000, 350);
					pa_active = 0;
					break;

				case 0x34: // '4' -> 2000 Hz High Pitch
					strncpy(status_msg, "PA Tone: 2000 Hz High", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, gain_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] Playing 2000 Hz High Pitch Beep via PA GPIO 31...\n");
					pa_play_acoustic_tone(2000, 350);
					pa_active = 0;
					break;

				case 0x35: // '5' -> Classic Nokia Ringtone
					strncpy(status_msg, "Playing: Nokia Tune!", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, gain_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] >>> Playing Iconic Nokia Ringtone via Loudspeaker! <<<\n");
					play_melody(melody_nokia, gain_level);
					pa_active = 0;
					break;

				case 0x36: // '6' -> Super Mario Coin
					strncpy(status_msg, "Playing: Mario Coin", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, gain_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] Playing Super Mario Coin Sound...\n");
					play_melody(melody_mario_coin, gain_level);
					pa_active = 0;
					break;

				case 0x37: // '7' -> Frequency Sweep / Siren
					strncpy(status_msg, "Playing: Audio Sweep", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, gain_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] Playing 300 Hz -> 2500 Hz Chirp Sweep...\n");
					play_chirp(gain_level);
					pa_active = 0;
					break;

				case 0x38: // '8' -> Speaker Siren & Click Test
					strncpy(status_msg, "PA Siren & Click Test", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, gain_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] Performing acoustic PA siren & cone click test...\n");
					speaker_siren_test();
					speaker_click_test(5);
					pa_active = 0;
					break;

				case 0x39: // '9' -> SC2720 Codec Diagnostic Dump
					dump_codec_registers();
					strncpy(status_msg, "Dumped Codec Regs", sizeof(status_msg) - 1);
					break;

				case 0x30: // '0' -> PMIC DAC Mode 2 Test
					strncpy(status_msg, "PMIC DAC LOUT Test", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, gain_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] Testing PMIC Codec Mode 2 Line-Out modulation (440 Hz)...\n");
					speaker_play_tone(440, 500, gain_level);
					pa_active = 0;
					break;

				case 0x24: // Volume Up -> Increase Line-Out Gain
					if (gain_level < 15) gain_level++;
					pmic_codec_set_gain(gain_level);
					snprintf(status_msg, sizeof(status_msg), "Gain: %d/15", gain_level);
					printf("  [Config] Line-Out gain set to %d/15\n", gain_level);
					pa_play_acoustic_tone(1200, 40); // Instant acoustic feedback blip
					break;

				case 0x25: // Volume Down -> Decrease Line-Out Gain
					if (gain_level > 0) gain_level--;
					pmic_codec_set_gain(gain_level);
					snprintf(status_msg, sizeof(status_msg), "Gain: %d/15", gain_level);
					printf("  [Config] Line-Out gain set to %d/15\n", gain_level);
					pa_play_acoustic_tone(800, 40); // Instant acoustic feedback blip
					break;

				case 0x2a: // '*' -> Stop all sounds / mute
					audio_pa_disable();
					strncpy(status_msg, "Audio Muted / Idle", sizeof(status_msg) - 1);
					printf("  [Audio] PA disabled and audio muted.\n");
					break;

				case 0x23: // '#' -> Super Mario Intro Jingle
					strncpy(status_msg, "Playing: Mario Intro", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, gain_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] Playing Super Mario Intro Jingle...\n");
					play_melody(melody_mario_intro, gain_level);
					pa_active = 0;
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
				printf("\n[+] Power/End button held. Exiting Audio payload cleanly!\n");
				break;
			}
		} else {
			pb_held_frames = 0;
		}

		// 3. Render frame
		draw_audio_dashboard(framebuf, w, h, frame, status_msg, gain_level, pa_active);

		// 4. Refresh display
		sys_start_refresh();
		sys_wait_refresh();

		// 5. Periodic heartbeat
		if ((frame % 90) == 0) {
			printf("  [Heartbeat] Audio payload alive | Uptime: %u sec | Mode: %s | Gain: %d/15\n",
			       frame / 30, status_msg, gain_level);
		}

		frame++;
	}

end_loop:
	audio_pa_disable();
	printf("\n[+] Audio test completed. Shutting down.\n");
}

// =============================================================================
// KEYPAD & SYSTEM TEST STUBS
// =============================================================================
static void test_keypad(void) {}
static int boot_cable_check(void) { return 0; }
static void usbio_bench(int arg, unsigned len, unsigned n) { (void)arg; (void)len; (void)n; }
static void sdio_bench(uint32_t pos, uint32_t size) { (void)pos; (void)size; }
static void sdio_write_test(unsigned i) { (void)i; }
static void test_sfc(void) {}
static void test_lzma(int flags) { (void)flags; }

static void test_timer(void) {
	unsigned t0, t1, delay = 100000;
	t0 = sys_timer_ms();
	sys_wait_us(delay);
	t1 = sys_timer_ms();
	printf("sys_wait_us(%u): %ums\n", delay, t1 - t0);
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

void eic_mask_or(unsigned val);

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
			test_audio();
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "keypad")) {
			test_keypad();
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "cpuid")) {
			test_cpuid();
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "timer")) {
			test_timer();
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "usb")) {
			usbio_bench(0, 1024, 1 << 20);
			usbio_bench(1, 1024, 1 << 20);
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "sfc")) {
			test_sfc();
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "lzma")) {
			test_lzma(0);
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "sdio")) {
			if (!sdtest_init(3)) {
				sdio_bench(1 << 20, 1 << 20);
				if (0) sdio_write_test(1);
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
