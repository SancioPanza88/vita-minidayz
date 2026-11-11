#pragma once
#include <stddef.h>
#include <stdint.h>
#include "quickjs.h"
#include "platform.h"

extern JSContext *g_ctx;

/* helpers */
void host_set_func(JSContext *ctx, JSValue obj, const char *name, JSCFunction *fn, int argc);
void *host_read_file(const char *path, size_t *size); /* malloc'd, NUL terminated */
char *host_game_path(const char *rel);                 /* malloc'd */
void host_logf(const char *fmt, ...);

/* module registration (each adds functions to the __n object) */
void natives_register(JSContext *ctx, JSValue n);
void gl_register(JSContext *ctx, JSValue n);
void audio_register(JSContext *ctx, JSValue n);
void audio_init(void);

/* software surfaces used by the 2D canvas emulation */
typedef struct {
	int w, h;
	uint8_t *px; /* RGBA, straight alpha */
} Surface;
Surface *surface_get(int id);

/* decode an image of the game folder to RGBA (malloc'd, free with free()) */
uint8_t *image_decode(const char *rel, int *w, int *h);
