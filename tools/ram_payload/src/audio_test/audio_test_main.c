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
// HARDWARE PERIPHERAL DRIVERS: SC2720 PMIC, GPIO & AUDIO PA
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

// External PA is routed through GPIO 31
#define AUDIO_PA_GPIO 31

void sys_wait_clk(uint32_t delay);

static void audio_pa_init(void) {
	// Configure GPIO 31 as output
	gpio_pin_set(AUDIO_PA_GPIO, 4, 1);    // GPIO_DMSK
	gpio_pin_set(AUDIO_PA_GPIO, 8, 1);    // GPIO_DIR = output
	gpio_pin_set(AUDIO_PA_GPIO, 0x28, 0); // GPIO_INEN = disabled
	gpio_pin_set(AUDIO_PA_GPIO, 0, 0);    // Start LOW (disabled)
}

static void audio_pa_set(int state) {
	gpio_pin_set(AUDIO_PA_GPIO, 0, state ? 1 : 0);
}

// Drive audio frequency on the speaker PA pin
static void speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
	if (freq_hz == 0 || duration_ms == 0) {
		if (duration_ms) sys_wait_ms(duration_ms);
		return;
	}

	// 26 MHz APB clock: half period in clock cycles
	uint32_t half_period_clk = 26000000 / (freq_hz * 2);
	if (half_period_clk < 200) half_period_clk = 200;
	uint32_t total_cycles = (freq_hz * duration_ms) / 1000;
	if (total_cycles == 0) total_cycles = 1;

	audio_pa_init();

	for (uint32_t i = 0; i < total_cycles; i++) {
		audio_pa_set(1);
		sys_wait_clk(half_period_clk);
		audio_pa_set(0);
		sys_wait_clk(half_period_clk);
	}
}

// =============================================================================
// SC2720 PMIC HAPTIC / VIBRATOR MOTOR DRIVER (0x40608e00 - LDO_VIBR)
// Disassembled from MOCOR kernel.bin @ 0x80be704c (analog_phy.c):
//   "Vibrator vlaue  = %d V,POWER_ON!!!!!!!!!!!!!!!"
//   "Vibrator vlaue  = %d,POWER_DOWN!!!!!!!!!!!!!!!"
// =============================================================================
#define PMIC_LDO_VIBR_REG 0x40608e00

// Enable or disable vibrator motor; level = 0..7 (2.8V .. 3.5V)
static void vibrator_set(int enable, int level) {
	if (enable) {
		if (level < 0) level = 0;
		if (level > 7) level = 7;
		uint32_t val = adi_read(PMIC_LDO_VIBR_REG);
		val = (val & ~0x7) | (level & 0x7);
		val &= ~0x300; // Clear bit 8 and bit 9 -> POWER ON
		adi_write(PMIC_LDO_VIBR_REG, val);
	} else {
		uint32_t val = adi_read(PMIC_LDO_VIBR_REG);
		val |= 0x300;  // Set bit 8 and bit 9 -> POWER DOWN
		adi_write(PMIC_LDO_VIBR_REG, val);
	}
}

static void vibrator_pulse(int duration_ms, int level) {
	vibrator_set(1, level);
	sys_wait_ms(duration_ms);
	vibrator_set(0, 0);
}

static void vibrator_morse_sos(int level) {
	// S: 3 short pulses (80ms on, 80ms off)
	for (int i = 0; i < 3; i++) {
		vibrator_pulse(80, level);
		sys_wait_ms(80);
	}
	sys_wait_ms(160);
	// O: 3 long pulses (240ms on, 80ms off)
	for (int i = 0; i < 3; i++) {
		vibrator_pulse(240, level);
		sys_wait_ms(80);
	}
	sys_wait_ms(160);
	// S: 3 short pulses (80ms on, 80ms off)
	for (int i = 0; i < 3; i++) {
		vibrator_pulse(80, level);
		sys_wait_ms(80);
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

static void play_melody(const note_t *notes) {
	while (notes->freq_hz || notes->duration_ms) {
		if (notes->freq_hz) {
			speaker_play_tone(notes->freq_hz, notes->duration_ms);
		} else {
			sys_wait_ms(notes->duration_ms);
		}
		sys_wait_ms(25); // tiny inter-note gap
		notes++;
	}
	audio_pa_set(0);
}

static void play_chirp(void) {
	for (uint32_t f = 300; f <= 2500; f += 40) {
		speaker_play_tone(f, 8);
	}
	audio_pa_set(0);
}

// =============================================================================
// LCD UI DASHBOARD
// =============================================================================
static void draw_audio_dashboard(uint16_t *fb, unsigned w, unsigned h, unsigned frame,
                                 const char *status_msg, int vib_level, int pa_active) {
	if (w <= 160) {
		// Outer Cover Screen Compact Mode (128x160)
		draw_fill_rect(fb, w, h, 0, 0, w, h, 0x0843);
		draw_fill_rect(fb, w, h, 0, 0, w, 20, 0x0010);
		draw_text_xy(fb, w, h, 4, 2, "AUDIO TEST (Cover)", 0x07FF, 0x0010, 1);

		draw_fill_rect(fb, w, h, 4, 24, w - 8, 48, 0x0000);
		draw_rect(fb, w, h, 4, 24, w - 8, 48, pa_active ? 0x07E0 : 0xFFE0);
		draw_text_xy(fb, w, h, 8, 28, status_msg, pa_active ? 0x07E0 : 0xFFFF, 0x0000, 1);
		char buf[32];
		snprintf(buf, sizeof(buf), "Vib Level: %d/15", vib_level);
		draw_text_xy(fb, w, h, 8, 48, buf, 0xCE79, 0x0000, 1);

		draw_text_xy(fb, w, h, 6, 80, "PA: GPIO 31", 0xFFFF, 0x0843, 1);
		draw_text_xy(fb, w, h, 6, 98, "PMIC: 0x40608df4", 0x07FF, 0x0843, 1);
		draw_text_xy(fb, w, h, 6, 120, "Hold END to exit", 0x8410, 0x0843, 1);

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
	draw_text_xy(fb, w, h, 14, 30, "TA-1480 | Speaker, PA & Haptics", 0xCE79, 0x0843, 1);

	// Card 1: Hardware Specs Box (48..84)
	draw_fill_rect(fb, w, h, 10, 48, w - 20, 36, 0x0000);
	draw_rect(fb, w, h, 10, 48, w - 20, 36, 0x03E0);
	draw_text_xy(fb, w, h, 16, 52, "Speaker PA: GPIO 31 (Bank 1)", 0x07E0, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 68, "PMIC Haptic: 0x40608df4 (ADI)", 0xFFFF, 0x0000, 1);

	// Card 2: Interactive Active Sound Card (88..160)
	uint16_t border_col = pa_active ? 0x07E0 : ((frame & 16) ? 0xFD20 : 0x001F);
	draw_fill_rect(fb, w, h, 10, 88, w - 20, 72, 0x0000);
	draw_rect(fb, w, h, 10, 88, w - 20, 72, border_col);
	draw_rect(fb, w, h, 12, 90, w - 24, 68, border_col);

	draw_text_xy(fb, w, h, 18, 96, "CURRENT ACTION:", 0x8410, 0x0000, 1);
	draw_text_xy(fb, w, h, 18, 114, status_msg, pa_active ? 0x07E0 : 0xFFE0, 0x0000, 1);

	char vbuf[32];
	snprintf(vbuf, sizeof(vbuf), "Vibrator Level: [%d/15]", vib_level);
	draw_text_xy(fb, w, h, 18, 136, vbuf, 0xCE79, 0x0000, 1);

	// Card 3: Keypad Control Matrix Guide (166..286)
	draw_fill_rect(fb, w, h, 10, 166, w - 20, 122, 0x0000);
	draw_rect(fb, w, h, 10, 166, w - 20, 122, 0x001F);

	draw_text_xy(fb, w, h, 16, 172, "KEYPAD CONTROLS:", 0xFD20, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 190, "[1] 440Hz Beep   [2] 880Hz Tone", 0xFFFF, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 206, "[3] 1000Hz Test  [4] 2000Hz High", 0xFFFF, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 222, "[5] Nokia Tune   [6] Mario Coin", 0x07E0, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 238, "[7] Audio Sweep  [8] Haptic Pulse", 0xFFE0, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 254, "[9] SOS Morse    [0] Heavy Vib", 0x07FF, 0x0000, 1);
	draw_text_xy(fb, w, h, 16, 270, "[*] Mute sound   [#] Mario Theme", 0xCE79, 0x0000, 1);

	// Footer bar
	draw_fill_rect(fb, w, h, 0, h - 22, w, 22, 0x0010);
	draw_text_xy(fb, w, h, 12, h - 18, "Hold Power/End to exit", 0x8410, 0x0010, 1);
	draw_text_xy(fb, w, h, w - 80, h - 18, "[READY]", 0x07E0, 0x0010, 1);
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

	// Initialize Audio PA & ensure vibrator starts in clean idle state
	audio_pa_init();
	vibrator_set(0, 0);

	printf("\n======================================================\n");
	printf("  NOKIA 2660 FLIP (TA-1480) - AUDIO & HAPTICS DRIVER\n");
	printf("  >>> SPEAKER PA (GPIO 31) & PMIC LDO_VIBR (0x40608e00) <<<\n");
	printf("======================================================\n");
	printf("  LCD: %ux%u | Volatile DRAM execution active\n", w, h);
	printf("  [1] 440 Hz Beep         [2] 880 Hz Tone\n");
	printf("  [3] 1000 Hz Beep        [4] 2000 Hz Tone\n");
	printf("  [5] Classic Nokia Tune  [6] Super Mario Coin\n");
	printf("  [7] Frequency Sweep     [8] Haptic Click (50ms)\n");
	printf("  [9] SOS Morse Vibration [0] Heavy Rumble (400ms)\n");
	printf("  [Vol+/Vol-] Adjust Vibrator Level (0..7)\n");
	printf("  [*] Stop Audio / Idle   [Hold END key] Exit payload\n\n");

	unsigned frame = 0;
	int vib_level = 7; // Default max haptic voltage (3.5V)
	int pa_active = 0;
	char status_msg[32] = "READY (Press 1-9 to Test)";

	int last_pb = read_power_button();
	int pb_held_frames = 0;

	// Draw and refresh first frame immediately so screen is active right away
	draw_audio_dashboard(framebuf, w, h, frame, status_msg, vib_level, pa_active);
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
				case 0x31: // '1' -> 440 Hz Beep
					strncpy(status_msg, "Playing: 440 Hz (Beep)", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, vib_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] Playing 440 Hz Concert A tone...\n");
					speaker_play_tone(440, 300);
					pa_active = 0;
					break;

				case 0x32: // '2' -> 880 Hz Tone
					strncpy(status_msg, "Playing: 880 Hz (A5)", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, vib_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] Playing 880 Hz High A tone...\n");
					speaker_play_tone(880, 300);
					pa_active = 0;
					break;

				case 0x33: // '3' -> 1000 Hz Test Beep
					strncpy(status_msg, "Playing: 1000 Hz Test", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, vib_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] Playing 1000 Hz Reference Beep...\n");
					speaker_play_tone(1000, 300);
					pa_active = 0;
					break;

				case 0x34: // '4' -> 2000 Hz High Pitch
					strncpy(status_msg, "Playing: 2000 Hz High", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, vib_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] Playing 2000 Hz High Pitch Beep...\n");
					speaker_play_tone(2000, 300);
					pa_active = 0;
					break;

				case 0x35: // '5' -> Classic Nokia Ringtone
					strncpy(status_msg, "Playing: Nokia Tune!", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, vib_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] >>> Playing Iconic Nokia Ringtone! <<<\n");
					play_melody(melody_nokia);
					pa_active = 0;
					break;

				case 0x36: // '6' -> Super Mario Coin
					strncpy(status_msg, "Playing: Mario Coin", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, vib_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] Playing Super Mario Coin Sound...\n");
					play_melody(melody_mario_coin);
					pa_active = 0;
					break;

				case 0x37: // '7' -> Frequency Sweep / Siren
					strncpy(status_msg, "Playing: Audio Sweep", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, vib_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] Playing 300 Hz -> 2500 Hz Chirp Sweep...\n");
					play_chirp();
					pa_active = 0;
					break;

				case 0x38: // '8' -> Haptic Click
					strncpy(status_msg, "Haptic: Click (50ms)", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, vib_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Haptics] Triggering short 50ms haptic click at level %d...\n", vib_level);
					vibrator_pulse(50, vib_level);
					pa_active = 0;
					break;

				case 0x39: // '9' -> SOS Morse Code Vibration
					strncpy(status_msg, "Haptic: SOS Morse Code", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, vib_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Haptics] Triggering SOS Morse Code vibration pattern...\n");
					vibrator_morse_sos(vib_level);
					pa_active = 0;
					break;

				case 0x30: // '0' -> Heavy Vibration Rumble
					strncpy(status_msg, "Haptic: Rumble (400ms)", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, vib_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Haptics] Triggering heavy 400ms motor rumble at level %d...\n", vib_level);
					vibrator_pulse(400, vib_level);
					pa_active = 0;
					break;

				case 0x24: // Volume Up -> Increase vibrator level
					if (vib_level < 7) vib_level++;
					snprintf(status_msg, sizeof(status_msg), "Vib Level: %d/7 (%d.%dV)", vib_level, (28 + vib_level) / 10, (28 + vib_level) % 10);
					vibrator_pulse(25, vib_level);
					printf("  [Config] Vibrator level set to %d/7 (%d.%dV)\n", vib_level, (28 + vib_level) / 10, (28 + vib_level) % 10);
					break;

				case 0x25: // Volume Down -> Decrease vibrator level
					if (vib_level > 0) vib_level--;
					snprintf(status_msg, sizeof(status_msg), "Vib Level: %d/7 (%d.%dV)", vib_level, (28 + vib_level) / 10, (28 + vib_level) % 10);
					if (vib_level > 0) vibrator_pulse(25, vib_level);
					printf("  [Config] Vibrator level set to %d/7 (%d.%dV)\n", vib_level, (28 + vib_level) / 10, (28 + vib_level) % 10);
					break;

				case 0x2a: // '*' -> Stop all sounds / idle
					audio_pa_set(0);
					vibrator_set(0, 0);
					strncpy(status_msg, "All Audio/Vib Muted", sizeof(status_msg) - 1);
					printf("  [Audio] All sound and vibration stopped.\n");
					break;

				case 0x23: // '#' -> Super Mario Intro Jingle
					strncpy(status_msg, "Playing: Mario Intro", sizeof(status_msg) - 1);
					pa_active = 1;
					draw_audio_dashboard(framebuf, w, h, frame, status_msg, vib_level, pa_active);
					sys_start_refresh(); sys_wait_refresh();
					printf("  [Audio] Playing Super Mario Intro Jingle...\n");
					play_melody(melody_mario_intro);
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
		draw_audio_dashboard(framebuf, w, h, frame, status_msg, vib_level, pa_active);

		// 4. Refresh display
		sys_start_refresh();
		sys_wait_refresh();

		// 5. Periodic heartbeat
		if ((frame % 90) == 0) {
			printf("  [Heartbeat] Audio payload alive | Uptime: %u sec | Mode: %s\n",
			       frame / 30, status_msg);
		}

		frame++;
		sys_wait_ms(16);
	}

end_loop:
	audio_pa_set(0);
	vibrator_set(0, 0);
	printf("\n[+] Audio test session finished cleanly.\n");
	free(framebuf_mem);
}

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

static void sdio_write_test(unsigned i) {
	void *mem = malloc(512 + 32);
	uint8_t *buf = (uint8_t*)(((intptr_t)mem + 31) & ~31);
	int ret = 0;
	do {
		uint32_t val;
		if (sdio_read_block(i, buf)) break;
		val = *(uint32_t*)buf;
		*(uint32_t*)buf = ~val;
		if (sdio_write_block(i, buf)) break;
		*(uint32_t*)buf = val ^ 0x55555555;
		if (sdio_read_block(i, buf)) break;
		if (*(uint32_t*)buf != ~val) break;
		ret = 1;
	} while (0);
	printf("sdio write %s\n", ret ? "ok" : "failed");
	free(mem);
}

static void test_keypad(void) {
	int type, key;
	for (;;) {
		type = sys_event(&key);
		switch (type) {
		case EVENT_KEYUP:
		case EVENT_KEYDOWN: {
			const char *s = "???";
			int power = key >> 15;
			key &= 0x7fff;
			switch (key) {
#define X(val, name) case val: s = #name; break;
			KEYPAD_ENUM(X)
#undef X
			}
			printf("%s0x%02x (\"%s\") %s\n", power ? "POWER + " : "",
					key, s, type == EVENT_KEYUP ? "release" : "press");
	    break;
		}
		case EVENT_END: sys_wait_ms(10); break;
		case EVENT_QUIT: goto end;
		}
	}
end:;
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

#if UMS9117
static void test_sfc(void) {}
static void test_lzma(int flags) { (void)flags; }
#else
static void test_sfc(void) {}
static void test_lzma(int flags) { (void)flags; }
#endif

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
