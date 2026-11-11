/*
 * Vita controls -> the touch input the mobile game expects.
 *  - front touch screen: passed through 1:1
 *  - left analog / D-pad: movement (a virtual finger dragged from the player)
 *  - right analog: moves an on-screen cursor; R (default) taps/drags at the cursor
 *  - face buttons etc.: tap a fixed point of the HUD while held
 * The mapping is written to ux0:data/MiniDayZ/controls.txt on first run and can be edited.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "host.h"
#include "input.h"
#include "glinc.h"

#define MODE_OFF 0
#define MODE_POINT 1
#define MODE_CURSOR 2

typedef struct { const char *name; int btn; int mode; float x, y; } BtnMap;

static BtnMap maps[] = {
	{ "cross", BTN_CROSS }, { "circle", BTN_CIRCLE }, { "square", BTN_SQUARE }, { "triangle", BTN_TRIANGLE },
	{ "l", BTN_L }, { "r", BTN_R }, { "start", BTN_START }, { "select", BTN_SELECT },
};
#define NMAPS (int)(sizeof(maps) / sizeof(maps[0]))

static float move_cx = 480, move_cy = 272, move_r = 110, deadzone = 0.25f;
static int dpad_move = 1;
static float cursor_speed = 650; /* px per second at full tilt */

#include "controls_default.h"

static void set_key(const char *key, const char *val) {
	float x, y;
	if (!strcmp(key, "move_center")) { if (sscanf(val, "%f , %f", &x, &y) == 2) { move_cx = x; move_cy = y; } return; }
	if (!strcmp(key, "move_radius")) { move_r = (float)atof(val); return; }
	if (!strcmp(key, "deadzone")) { deadzone = (float)atof(val); return; }
	if (!strcmp(key, "dpad_move")) { dpad_move = atoi(val); return; }
	if (!strcmp(key, "cursor_speed")) { cursor_speed = (float)atof(val); return; }
	for (int i = 0; i < NMAPS; i++) {
		if (strcmp(key, maps[i].name)) continue;
		if (!strncmp(val, "cursor", 6)) maps[i].mode = MODE_CURSOR;
		else if (sscanf(val, "%f , %f", &x, &y) == 2 && x >= 0) { maps[i].mode = MODE_POINT; maps[i].x = x; maps[i].y = y; }
		else maps[i].mode = MODE_OFF;
		return;
	}
}

static void parse_config(const char *txt) {
	char line[256];
	while (*txt) {
		int n = 0;
		while (*txt && *txt != '\n' && n < 255) line[n++] = *txt++;
		if (*txt == '\n') txt++;
		line[n] = 0;
		char *hash = strchr(line, '#');
		if (hash) *hash = 0;
		char key[64], val[128];
		if (sscanf(line, " %63[a-z_] = %127[^\r\n]", key, val) == 2) set_key(key, val);
	}
}

void input_load_config(void) {
	parse_config(controls_default);
	char p[512];
	snprintf(p, sizeof(p), "%scontrols.txt", plat_save_dir());
	char *txt = host_read_file(p, NULL);
	if (!txt) {
		FILE *f = fopen(p, "wb");
		if (f) { fputs(controls_default, f); fclose(f); }
		return;
	}
	parse_config(txt);
	free(txt);
}

/* ---- virtual fingers ---- */
typedef struct { int down; float x, y; } Finger;
#define VF_MOVE 0
#define VF_BTN 1
static Finger vf[VF_BTN + NMAPS];
static int prev_touch_id[MAX_TOUCH], prev_touch_count;

static void touch_event(int type, int id, float x, float y) {
	double a[4] = { type, id, x, y };
	host_call("__touch", 4, a);
}

static void finger(int idx, int down, float x, float y) {
	Finger *f = &vf[idx];
	int id = 1000 + idx;
	if (down && !f->down) touch_event(0, id, x, y);
	else if (down && f->down && (fabsf(f->x - x) > 0.01f || fabsf(f->y - y) > 0.01f)) touch_event(1, id, x, y);
	else if (!down && f->down) touch_event(2, id, f->x, f->y);
	f->down = down;
	if (down) { f->x = x; f->y = y; }
}

/* ---- cursor ---- */
static float cur_x = 480, cur_y = 272;
static double cur_last_ms = -1e9, last_ms;
static int cursor_visible(void) { return last_ms - cur_last_ms < 4000; }

void input_process(InputState *in) {
	double now = plat_now_ms();
	double dt = last_ms > 0 ? (now - last_ms) / 1000.0 : 0;
	if (dt > 0.1) dt = 0.1;
	last_ms = now;

	/* real touches */
	for (int i = 0; i < prev_touch_count; i++) {
		int still = 0;
		for (int j = 0; j < in->touch_count; j++) if (in->touch_id[j] == prev_touch_id[i]) still = 1;
		if (!still) touch_event(2, prev_touch_id[i], -1, -1);
	}
	for (int j = 0; j < in->touch_count; j++) {
		int was = 0;
		for (int i = 0; i < prev_touch_count; i++) if (in->touch_id[j] == prev_touch_id[i]) was = 1;
		touch_event(was ? 1 : 0, in->touch_id[j], in->touch_x[j], in->touch_y[j]);
	}
	prev_touch_count = in->touch_count;
	memcpy(prev_touch_id, in->touch_id, sizeof(prev_touch_id));

	/* movement: left stick, or D-pad */
	float sx = in->lx, sy = in->ly;
	if (dpad_move) {
		float dx = (float)in->buttons[BTN_RIGHT] - in->buttons[BTN_LEFT];
		float dy = (float)in->buttons[BTN_DOWN] - in->buttons[BTN_UP];
		if (dx || dy) { float m = sqrtf(dx * dx + dy * dy); sx = dx / m; sy = dy / m; }
	}
	float m = sqrtf(sx * sx + sy * sy);
	if (m < deadzone) finger(VF_MOVE, 0, 0, 0);
	else {
		if (m > 1) { sx /= m; sy /= m; }
		/* first contact just next to the player, then drag in the stick direction */
		if (!vf[VF_MOVE].down) finger(VF_MOVE, 1, move_cx + sx * 8, move_cy + sy * 8);
		finger(VF_MOVE, 1, move_cx + sx * move_r, move_cy + sy * move_r);
	}

	/* cursor: right stick */
	float rx = in->rx, ry = in->ry, rm = sqrtf(rx * rx + ry * ry);
	if (rm > deadzone) {
		float k = (rm - deadzone) / (1 - deadzone);
		cur_x += rx / rm * k * k * cursor_speed * (float)dt;
		cur_y += ry / rm * k * k * cursor_speed * (float)dt;
		if (cur_x < 0) cur_x = 0;
		if (cur_y < 0) cur_y = 0;
		if (cur_x > SCREEN_W - 1) cur_x = SCREEN_W - 1;
		if (cur_y > SCREEN_H - 1) cur_y = SCREEN_H - 1;
		cur_last_ms = now;
	}

	for (int i = 0; i < NMAPS; i++) {
		int pressed = in->buttons[maps[i].btn];
		if (maps[i].mode == MODE_POINT) finger(VF_BTN + i, pressed, maps[i].x, maps[i].y);
		else if (maps[i].mode == MODE_CURSOR) {
			if (pressed) cur_last_ms = now;
			finger(VF_BTN + i, pressed, cur_x, cur_y);
		}
	}
}

/* Cursor drawn with scissored clears: no shaders or extra GL state needed. */
static void rect(int x, int y, int w, int h, float r, float g, float b) {
	glScissor(x, SCREEN_H - y - h, w, h);
	glClearColor(r, g, b, 1);
	glClear(GL_COLOR_BUFFER_BIT);
}

void overlay_draw(void) {
	if (!cursor_visible()) return;
	int x = (int)cur_x, y = (int)cur_y;
	GLboolean sc = glIsEnabled(GL_SCISSOR_TEST);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, SCREEN_W, SCREEN_H);
	glEnable(GL_SCISSOR_TEST);
	rect(x - 10, y - 2, 21, 5, 0, 0, 0);
	rect(x - 2, y - 10, 5, 21, 0, 0, 0);
	rect(x - 9, y - 1, 19, 3, 1, 1, 1);
	rect(x - 1, y - 9, 3, 19, 1, 1, 1);
	if (!sc) glDisable(GL_SCISSOR_TEST);
	glClearColor(0, 0, 0, 0);
}
