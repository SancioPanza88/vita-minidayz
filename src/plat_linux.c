/*
 * Linux test platform: headless EGL (Mesa) + GLES2, deterministic clock, scripted input
 * and PNG screenshots. Used to test the port without a Vita.
 *
 * Environment:
 *   MDZ_GAME_DIR  game export folder (default ./game/)
 *   MDZ_RES_DIR   runtime resources (default ./res/)
 *   MDZ_SAVE_DIR  writable folder (default ./save/)
 *   MDZ_FRAMES    stop after N frames (default 600)
 *   MDZ_SHOTS     comma separated frame numbers to screenshot (shot_<n>.png)
 *   MDZ_INPUT     input script: "<frame> down|move <id> <x> <y>", "<frame> up <id>",
 *                 "<frame> btn <name> <0|1>", "<frame> stick <lx> <ly>"
 *   MDZ_REALTIME  use the wall clock instead of a fixed 60 fps clock
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "platform.h"
#include "glinc.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static EGLDisplay dpy;
static EGLSurface surf;
static EGLContext ctxt;
static int max_frames = 600, realtime;
static char game_dir[512], res_dir[512], save_dir[512];
static double t0, cpu_acc, cpu_max;
static int frame_now;
static AudioMixFn mixfn;
static char *input_script;

static double wall_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void dir_env(char *dst, const char *env, const char *def) {
	const char *v = getenv(env);
	snprintf(dst, 512, "%s", v ? v : def);
	size_t n = strlen(dst);
	if (n && dst[n - 1] != '/') strcat(dst, "/");
}

int plat_init(void) {
	dir_env(game_dir, "MDZ_GAME_DIR", "./game/");
	dir_env(res_dir, "MDZ_RES_DIR", "./res/");
	dir_env(save_dir, "MDZ_SAVE_DIR", "./save/");
	if (getenv("MDZ_FRAMES")) max_frames = atoi(getenv("MDZ_FRAMES"));
	realtime = getenv("MDZ_REALTIME") != NULL;
	if (getenv("MDZ_INPUT")) {
		FILE *f = fopen(getenv("MDZ_INPUT"), "rb");
		if (f) {
			fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
			input_script = calloc(1, n + 1);
			if (fread(input_script, 1, n, f) != (size_t)n) n = 0;
			fclose(f);
		}
	}

	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	dpy = gpd ? gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL) : eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!eglInitialize(dpy, NULL, NULL)) { fprintf(stderr, "eglInitialize failed\n"); return -1; }
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint cattr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
	EGLConfig cfg; EGLint nc = 0;
	if (!eglChooseConfig(dpy, cattr, &cfg, 1, &nc) || nc < 1) { fprintf(stderr, "no EGL config\n"); return -1; }
	EGLint sattr[] = { EGL_WIDTH, SCREEN_W, EGL_HEIGHT, SCREEN_H, EGL_NONE };
	surf = eglCreatePbufferSurface(dpy, cfg, sattr);
	EGLint xattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	ctxt = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xattr);
	if (!eglMakeCurrent(dpy, surf, surf, ctxt)) { fprintf(stderr, "eglMakeCurrent failed\n"); return -1; }
	fprintf(stderr, "GL: %s / %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
	t0 = wall_ms();
	return 0;
}

void plat_shutdown(void) {
	fprintf(stderr, "frames: %d, avg cpu/frame %.2f ms, max %.2f ms\n", frame_now, cpu_acc / (frame_now ? frame_now : 1), cpu_max);
	eglTerminate(dpy);
}

static double frame_start;

static int btn_index(const char *n) {
	static const char *names[] = { "cross", "circle", "square", "triangle", "l", "r", "start", "select", "up", "down", "left", "right" };
	for (int i = 0; i < BTN_COUNT; i++) if (!strcmp(n, names[i])) return i;
	return -1;
}

void plat_poll(InputState *in, int frame) {
	frame_now = frame;
	frame_start = wall_ms();
	if (frame >= max_frames) { in->quit = 1; return; }
	if (!input_script) return;
	const char *p = input_script;
	char line[256];
	while (*p) {
		int n = 0;
		while (*p && *p != '\n' && n < 255) line[n++] = *p++;
		if (*p) p++;
		line[n] = 0;
		int f, id; float x, y; char cmd[32], name[32];
		if (sscanf(line, "%d %31s", &f, cmd) != 2 || f != frame) continue;
		if (!strcmp(cmd, "down") || !strcmp(cmd, "move")) {
			if (sscanf(line, "%*d %*s %d %f %f", &id, &x, &y) != 3) continue;
			int k;
			for (k = 0; k < in->touch_count; k++) if (in->touch_id[k] == id) break;
			if (k == in->touch_count && k < MAX_TOUCH) in->touch_count++;
			in->touch_id[k] = id; in->touch_x[k] = x; in->touch_y[k] = y;
		} else if (!strcmp(cmd, "up")) {
			if (sscanf(line, "%*d %*s %d", &id) != 1) continue;
			for (int k = 0; k < in->touch_count; k++) if (in->touch_id[k] == id) {
				in->touch_count--;
				in->touch_id[k] = in->touch_id[in->touch_count];
				in->touch_x[k] = in->touch_x[in->touch_count];
				in->touch_y[k] = in->touch_y[in->touch_count];
				break;
			}
		} else if (!strcmp(cmd, "btn")) {
			int v;
			if (sscanf(line, "%*d %*s %31s %d", name, &v) != 2) continue;
			int b = btn_index(name);
			if (b >= 0) in->buttons[b] = v;
		} else if (!strcmp(cmd, "stick")) {
			if (sscanf(line, "%*d %*s %f %f", &x, &y) != 2) continue;
			in->lx = x; in->ly = y;
		}
	}
}

static void screenshot(const char *name) {
	unsigned char *px = malloc(SCREEN_W * SCREEN_H * 4);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glReadPixels(0, 0, SCREEN_W, SCREEN_H, GL_RGBA, GL_UNSIGNED_BYTE, px);
	for (int i = 0; i < SCREEN_W * SCREEN_H; i++) px[i * 4 + 3] = 255;
	stbi_flip_vertically_on_write(1);
	stbi_write_png(name, SCREEN_W, SCREEN_H, 4, px, SCREEN_W * 4);
	free(px);
	fprintf(stderr, "screenshot %s\n", name);
}

void plat_swap(int frame) {
	double cpu = wall_ms() - frame_start;
	cpu_acc += cpu;
	if (cpu > cpu_max && frame > 30) cpu_max = cpu;
	if (frame % 300 == 299) fprintf(stderr, "[frame %d] avg cpu %.2f ms\n", frame, cpu_acc / (frame + 1));
	const char *shots = getenv("MDZ_SHOTS");
	if (shots) {
		char buf[1024];
		snprintf(buf, sizeof(buf), ",%s,", shots);
		char key[32];
		snprintf(key, sizeof(key), ",%d,", frame);
		if (strstr(buf, key)) { char name[64]; snprintf(name, sizeof(name), "shot_%05d.png", frame); screenshot(name); }
	}
	if (mixfn) { static int16_t tmp[800 * 2]; mixfn(tmp, 800); }
	eglSwapBuffers(dpy, surf);
}

double plat_now_ms(void) {
	if (realtime) return wall_ms() - t0;
	return frame_now * (1000.0 / 60.0);
}

void plat_audio_start(AudioMixFn fn) { mixfn = fn; }
void plat_audio_lock(void) {}
void plat_audio_unlock(void) {}
const char *plat_game_dir(void) { return game_dir; }
const char *plat_res_dir(void) { return res_dir; }
const char *plat_save_dir(void) { return save_dir; }
void plat_log(const char *s) { fprintf(stderr, "%s\n", s); }
const char *plat_lang(void) { const char *l = getenv("MDZ_LANG"); return l ? l : "en-US"; }
void plat_fatal(const char *msg) { fprintf(stderr, "FATAL: %s\n", msg); exit(1); }
