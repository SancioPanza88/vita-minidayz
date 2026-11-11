/* Platform abstraction: PS Vita (vitaGL) or Linux test host (headless EGL). */
#pragma once
#include <stdint.h>

#define SCREEN_W 960
#define SCREEN_H 544

/* Physical buttons, platform independent. */
enum {
	BTN_CROSS, BTN_CIRCLE, BTN_SQUARE, BTN_TRIANGLE,
	BTN_L, BTN_R, BTN_START, BTN_SELECT,
	BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
	BTN_COUNT
};

#define MAX_TOUCH 6

typedef struct {
	int touch_count;
	int touch_id[MAX_TOUCH];
	float touch_x[MAX_TOUCH], touch_y[MAX_TOUCH]; /* screen pixels */
	uint8_t buttons[BTN_COUNT];
	float lx, ly, rx, ry; /* analog sticks, -1..1 */
	int quit;
} InputState;

typedef void (*AudioMixFn)(int16_t *out, int frames); /* stereo, 48000 Hz */

int  plat_init(void);
void plat_shutdown(void);
void plat_poll(InputState *in, int frame);
void plat_swap(int frame);
double plat_now_ms(void);
void plat_audio_start(AudioMixFn fn);
void plat_audio_lock(void);
void plat_audio_unlock(void);
const char *plat_game_dir(void);   /* read-only game data, trailing slash */
const char *plat_res_dir(void);    /* our own resources (prelude, font) */
const char *plat_save_dir(void);   /* writable, trailing slash */
void plat_log(const char *s);
const char *plat_lang(void);        /* e.g. "it-IT" */
void overlay_draw(void);              /* input.c: cursor drawn over the game */
