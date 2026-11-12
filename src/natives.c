/* File access, persistent storage, images and the software 2D canvas. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "host.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

/* ---------- files ---------- */

static JSValue js_read_text(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	const char *rel = JS_ToCString(ctx, argv[0]);
	if (!rel) return JS_NULL;
	char *p = host_game_path(rel);
	JS_FreeCString(ctx, rel);
	size_t sz;
	char *buf = host_read_file(p, &sz);
	free(p);
	if (!buf) return JS_NULL;
	char *s = buf;
	if (sz >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) { s += 3; sz -= 3; }
	JSValue v = JS_NewStringLen(ctx, s, sz);
	free(buf);
	return v;
}

static void *free_ab(JSRuntime *rt, void *opaque, void *ptr, size_t size) {
	if (size == 0) { free(ptr); return NULL; }
	return realloc(ptr, size);
}

static JSValue js_read_binary(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	const char *rel = JS_ToCString(ctx, argv[0]);
	if (!rel) return JS_NULL;
	char *p = host_game_path(rel);
	JS_FreeCString(ctx, rel);
	size_t sz;
	char *buf = host_read_file(p, &sz);
	free(p);
	if (!buf) return JS_NULL;
	return JS_NewArrayBuffer(ctx, (uint8_t *)buf, sz, 0, free_ab, NULL, false);
}

static char *save_file_path(void) {
	static char p[512];
	snprintf(p, sizeof(p), "%sstorage.json", plat_save_dir());
	return p;
}

static JSValue js_storage_load(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	size_t sz;
	char *buf = host_read_file(save_file_path(), &sz);
	if (!buf) return JS_NULL;
	JSValue v = JS_NewStringLen(ctx, buf, sz);
	free(buf);
	return v;
}

static JSValue js_storage_save(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	size_t len;
	const char *s = JS_ToCStringLen(ctx, &len, argv[0]);
	if (!s) return JS_FALSE;
	char tmp[520];
	snprintf(tmp, sizeof(tmp), "%s.tmp", save_file_path());
	FILE *f = fopen(tmp, "wb");
	int ok = 0;
	if (f) {
		ok = fwrite(s, 1, len, f) == len;
		fclose(f);
		if (ok) {
			remove(save_file_path());
			ok = rename(tmp, save_file_path()) == 0;
		}
	}
	JS_FreeCString(ctx, s);
	if (!ok) host_logf("storage save failed");
	return JS_NewBool(ctx, ok);
}

static JSValue js_now(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	return JS_NewFloat64(ctx, plat_now_ms());
}

/* ---------- images ---------- */

uint8_t *image_decode(const char *rel, int *w, int *h) {
	char *p = host_game_path(rel);
	size_t sz;
	unsigned char *buf = host_read_file(p, &sz);
	if (!buf) { host_logf("image not found: %s", p); free(p); return NULL; }
	free(p);
	int comp;
	uint8_t *px = stbi_load_from_memory(buf, (int)sz, w, h, &comp, 4);
	free(buf);
	if (!px) host_logf("image decode failed: %s", rel);
	return px;
}

/* returns [w,h] reading only the header */
static JSValue js_image_info(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	const char *rel = JS_ToCString(ctx, argv[0]);
	if (!rel) return JS_NULL;
	char *p = host_game_path(rel);
	JS_FreeCString(ctx, rel);
	FILE *f = fopen(p, "rb");
	free(p);
	if (!f) return JS_NULL;
	static unsigned char hdr[64 * 1024];
	size_t n = fread(hdr, 1, 32, f);
	/* PNG: size is in the IHDR chunk, no need to read more */
	if (n >= 24 && hdr[0] == 0x89 && hdr[1] == 'P' && hdr[2] == 'N' && hdr[3] == 'G') {
		fclose(f);
		JSValue a = JS_NewArray(ctx);
		JS_SetPropertyUint32(ctx, a, 0, JS_NewInt32(ctx, (hdr[16] << 24) | (hdr[17] << 16) | (hdr[18] << 8) | hdr[19]));
		JS_SetPropertyUint32(ctx, a, 1, JS_NewInt32(ctx, (hdr[20] << 24) | (hdr[21] << 16) | (hdr[22] << 8) | hdr[23]));
		return a;
	}
	n += fread(hdr + n, 1, sizeof(hdr) - n, f);
	fclose(f);
	int w, h, c;
	if (!stbi_info_from_memory(hdr, (int)n, &w, &h, &c)) return JS_NULL;
	JSValue a = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, a, 0, JS_NewInt32(ctx, w));
	JS_SetPropertyUint32(ctx, a, 1, JS_NewInt32(ctx, h));
	return a;
}

/* ---------- surfaces (2D canvas backing store) ---------- */

#define MAX_SURF 4096
static Surface *surfs[MAX_SURF];

Surface *surface_get(int id) {
	if (id <= 0 || id >= MAX_SURF) return NULL;
	return surfs[id];
}

static void surf_alloc(Surface *s, int w, int h) {
	free(s->px);
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	s->w = w; s->h = h;
	s->px = calloc((size_t)w * h, 4);
}

static JSValue js_surf_create(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	int w = 0, h = 0;
	JS_ToInt32(ctx, &w, argv[0]);
	JS_ToInt32(ctx, &h, argv[1]);
	for (int i = 1; i < MAX_SURF; i++) {
		if (!surfs[i]) {
			surfs[i] = calloc(1, sizeof(Surface));
			surf_alloc(surfs[i], w, h);
			return JS_NewInt32(ctx, i);
		}
	}
	host_logf("out of surfaces");
	return JS_NewInt32(ctx, 0);
}

static JSValue js_surf_resize(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	int id, w, h;
	JS_ToInt32(ctx, &id, argv[0]); JS_ToInt32(ctx, &w, argv[1]); JS_ToInt32(ctx, &h, argv[2]);
	Surface *s = surface_get(id);
	if (s) surf_alloc(s, w, h);
	return JS_UNDEFINED;
}

static JSValue js_surf_free(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	int id;
	JS_ToInt32(ctx, &id, argv[0]);
	Surface *s = surface_get(id);
	if (s) { free(s->px); free(s); surfs[id] = NULL; }
	return JS_UNDEFINED;
}

static inline void blend_px(uint8_t *d, int r, int g, int b, int a) {
	if (a <= 0) return;
	if (a >= 255) { d[0] = r; d[1] = g; d[2] = b; d[3] = 255; return; }
	int da = d[3];
	int oa = a + da * (255 - a) / 255;
	if (oa <= 0) return;
	d[0] = (r * a + d[0] * da * (255 - a) / 255) / oa;
	d[1] = (g * a + d[1] * da * (255 - a) / 255) / oa;
	d[2] = (b * a + d[2] * da * (255 - a) / 255) / oa;
	d[3] = oa;
}

static void clip_rect(Surface *s, int *x0, int *y0, int *x1, int *y1) {
	if (*x0 < 0) *x0 = 0;
	if (*y0 < 0) *y0 = 0;
	if (*x1 > s->w) *x1 = s->w;
	if (*y1 > s->h) *y1 = s->h;
}

/* clearRect(id, x, y, w, h) */
static JSValue js_surf_clear(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	int id; double x, y, w, h;
	JS_ToInt32(ctx, &id, argv[0]);
	JS_ToFloat64(ctx, &x, argv[1]); JS_ToFloat64(ctx, &y, argv[2]);
	JS_ToFloat64(ctx, &w, argv[3]); JS_ToFloat64(ctx, &h, argv[4]);
	Surface *s = surface_get(id);
	if (!s) return JS_UNDEFINED;
	int x0 = (int)floor(x), y0 = (int)floor(y), x1 = (int)ceil(x + w), y1 = (int)ceil(y + h);
	clip_rect(s, &x0, &y0, &x1, &y1);
	for (int j = y0; j < y1; j++)
		if (x1 > x0) memset(s->px + ((size_t)j * s->w + x0) * 4, 0, (size_t)(x1 - x0) * 4);
	return JS_UNDEFINED;
}

/* fillRect(id, x, y, w, h, r, g, b, a(0..255)) */
static JSValue js_surf_fill(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	int id, r, g, b, a; double x, y, w, h;
	JS_ToInt32(ctx, &id, argv[0]);
	JS_ToFloat64(ctx, &x, argv[1]); JS_ToFloat64(ctx, &y, argv[2]);
	JS_ToFloat64(ctx, &w, argv[3]); JS_ToFloat64(ctx, &h, argv[4]);
	JS_ToInt32(ctx, &r, argv[5]); JS_ToInt32(ctx, &g, argv[6]); JS_ToInt32(ctx, &b, argv[7]); JS_ToInt32(ctx, &a, argv[8]);
	Surface *s = surface_get(id);
	if (!s) return JS_UNDEFINED;
	if (w < 0) { x += w; w = -w; }
	if (h < 0) { y += h; h = -h; }
	int x0 = (int)floor(x + 0.5), y0 = (int)floor(y + 0.5), x1 = (int)floor(x + w + 0.5), y1 = (int)floor(y + h + 0.5);
	clip_rect(s, &x0, &y0, &x1, &y1);
	for (int j = y0; j < y1; j++)
		for (int i = x0; i < x1; i++) blend_px(s->px + ((size_t)j * s->w + i) * 4, r, g, b, a);
	return JS_UNDEFINED;
}

/* drawImage(id, srcKind (0=image path, 1=surface id), src, sx, sy, sw, sh, dx, dy, dw, dh, alpha(0..1)) */
static JSValue js_surf_draw(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	int id, kind;
	double sx, sy, sw, sh, dx, dy, dw, dh, alpha;
	JS_ToInt32(ctx, &id, argv[0]);
	JS_ToInt32(ctx, &kind, argv[1]);
	JS_ToFloat64(ctx, &sx, argv[3]); JS_ToFloat64(ctx, &sy, argv[4]);
	JS_ToFloat64(ctx, &sw, argv[5]); JS_ToFloat64(ctx, &sh, argv[6]);
	JS_ToFloat64(ctx, &dx, argv[7]); JS_ToFloat64(ctx, &dy, argv[8]);
	JS_ToFloat64(ctx, &dw, argv[9]); JS_ToFloat64(ctx, &dh, argv[10]);
	JS_ToFloat64(ctx, &alpha, argv[11]);
	Surface *d = surface_get(id);
	if (!d || dw == 0 || dh == 0 || sw == 0 || sh == 0) return JS_UNDEFINED;
	uint8_t *src = NULL; int w = 0, h = 0, own = 0;
	if (kind == 0) {
		const char *rel = JS_ToCString(ctx, argv[2]);
		if (rel) { src = image_decode(rel, &w, &h); own = 1; JS_FreeCString(ctx, rel); }
	} else {
		int sid; JS_ToInt32(ctx, &sid, argv[2]);
		Surface *s = surface_get(sid);
		if (s) { src = s->px; w = s->w; h = s->h; }
	}
	if (!src) return JS_UNDEFINED;
	int a = (int)(alpha * 255.0 + 0.5);
	int x0 = (int)floor(dx), y0 = (int)floor(dy), x1 = (int)ceil(dx + dw), y1 = (int)ceil(dy + dh);
	clip_rect(d, &x0, &y0, &x1, &y1);
	for (int j = y0; j < y1; j++) {
		int syi = (int)floor(sy + ((j + 0.5) - dy) * sh / dh);
		if (syi < 0 || syi >= h) continue;
		for (int i = x0; i < x1; i++) {
			int sxi = (int)floor(sx + ((i + 0.5) - dx) * sw / dw);
			if (sxi < 0 || sxi >= w) continue;
			const uint8_t *p = src + ((size_t)syi * w + sxi) * 4;
			blend_px(d->px + ((size_t)j * d->w + i) * 4, p[0], p[1], p[2], p[3] * a / 255);
		}
	}
	if (own) stbi_image_free(src);
	return JS_UNDEFINED;
}

/* getImageData(id, x, y, w, h) -> ArrayBuffer RGBA */
static JSValue js_surf_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	int id, x, y, w, h;
	JS_ToInt32(ctx, &id, argv[0]); JS_ToInt32(ctx, &x, argv[1]); JS_ToInt32(ctx, &y, argv[2]);
	JS_ToInt32(ctx, &w, argv[3]); JS_ToInt32(ctx, &h, argv[4]);
	Surface *s = surface_get(id);
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	uint8_t *out = calloc((size_t)w * h, 4);
	if (s) for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
		int X = x + i, Y = y + j;
		if (X >= 0 && Y >= 0 && X < s->w && Y < s->h) memcpy(out + ((size_t)j * w + i) * 4, s->px + ((size_t)Y * s->w + X) * 4, 4);
	}
	return JS_NewArrayBuffer(ctx, out, (size_t)w * h * 4, 0, free_ab, NULL, false);
}

/* ---------- text ---------- */

static stbtt_fontinfo font;
static int font_ok;
static unsigned char *font_data;

static void font_init(void) {
	if (font_ok) return;
	char p[512];
	snprintf(p, sizeof(p), "%sLiberationSans-Regular.ttf", plat_res_dir());
	font_data = host_read_file(p, NULL);
	if (font_data && stbtt_InitFont(&font, font_data, stbtt_GetFontOffsetForIndex(font_data, 0))) font_ok = 1;
	else host_logf("font load failed: %s", p);
}

static int utf8_next(const char **ps) {
	const unsigned char *s = (const unsigned char *)*ps;
	int c = *s++;
	if (c >= 0xF0 && s[0] && s[1] && s[2]) { c = ((c & 7) << 18) | ((s[0] & 63) << 12) | ((s[1] & 63) << 6) | (s[2] & 63); s += 3; }
	else if (c >= 0xE0 && s[0] && s[1]) { c = ((c & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63); s += 2; }
	else if (c >= 0xC0 && s[0]) { c = ((c & 31) << 6) | (s[0] & 63); s += 1; }
	*ps = (const char *)s;
	return c;
}

static double text_width(const char *t, double px) {
	font_init();
	if (!font_ok) return strlen(t) * px * 0.5;
	float sc = stbtt_ScaleForMappingEmToPixels(&font, (float)px);
	double w = 0;
	int prev = 0;
	while (*t) {
		int c = utf8_next(&t);
		int adv, lsb;
		stbtt_GetCodepointHMetrics(&font, c, &adv, &lsb);
		if (prev) w += sc * stbtt_GetCodepointKernAdvance(&font, prev, c);
		w += sc * adv;
		prev = c;
	}
	return w;
}

static JSValue js_text_width(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	const char *t = JS_ToCString(ctx, argv[0]);
	double px; JS_ToFloat64(ctx, &px, argv[1]);
	double w = t ? text_width(t, px) : 0;
	if (t) JS_FreeCString(ctx, t);
	return JS_NewFloat64(ctx, w);
}

/* fillText(id, text, x, y, px, r, g, b, a, align(0 l,1 c,2 r), baseline(0 alpha,1 top,2 middle,3 bottom), bold) */
static JSValue js_surf_text(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	int id, r, g, b, a, align, base, bold;
	double x, y, px;
	JS_ToInt32(ctx, &id, argv[0]);
	JS_ToFloat64(ctx, &x, argv[2]); JS_ToFloat64(ctx, &y, argv[3]); JS_ToFloat64(ctx, &px, argv[4]);
	JS_ToInt32(ctx, &r, argv[5]); JS_ToInt32(ctx, &g, argv[6]); JS_ToInt32(ctx, &b, argv[7]); JS_ToInt32(ctx, &a, argv[8]);
	JS_ToInt32(ctx, &align, argv[9]); JS_ToInt32(ctx, &base, argv[10]); JS_ToInt32(ctx, &bold, argv[11]);
	Surface *s = surface_get(id);
	font_init();
	if (!s || !font_ok || px <= 0) return JS_UNDEFINED;
	const char *t = JS_ToCString(ctx, argv[1]);
	if (!t) return JS_UNDEFINED;
	float sc = stbtt_ScaleForMappingEmToPixels(&font, (float)px);
	int asc, desc, gap;
	stbtt_GetFontVMetrics(&font, &asc, &desc, &gap);
	double w = text_width(t, px);
	if (align == 1) x -= w / 2;
	else if (align == 2) x -= w;
	double baseline = y;
	if (base == 1) baseline = y + asc * sc;
	else if (base == 2) baseline = y + (asc + desc) * sc / 2;
	else if (base == 3) baseline = y + desc * sc;
	const char *p = t;
	double pen = x;
	int prev = 0;
	while (*p) {
		int c = utf8_next(&p);
		int adv, lsb;
		stbtt_GetCodepointHMetrics(&font, c, &adv, &lsb);
		if (prev) pen += sc * stbtt_GetCodepointKernAdvance(&font, prev, c);
		int gw, gh, xo, yo;
		float fx = (float)(pen - floor(pen));
		unsigned char *bm = stbtt_GetCodepointBitmapSubpixel(&font, sc, sc, fx, 0, c, &gw, &gh, &xo, &yo);
		if (bm) {
			int ox = (int)floor(pen) + xo, oy = (int)floor(baseline + 0.5) + yo;
			for (int pass = 0; pass <= (bold ? 1 : 0); pass++)
				for (int j = 0; j < gh; j++) {
					int Y = oy + j;
					if (Y < 0 || Y >= s->h) continue;
					for (int i = 0; i < gw; i++) {
						int X = ox + i + pass;
						if (X < 0 || X >= s->w) continue;
						int cov = bm[j * gw + i];
						if (cov) blend_px(s->px + ((size_t)Y * s->w + X) * 4, r, g, b, cov * a / 255);
					}
				}
			stbtt_FreeBitmap(bm, NULL);
		}
		pen += sc * adv;
		prev = c;
	}
	JS_FreeCString(ctx, t);
	return JS_UNDEFINED;
}

void natives_register(JSContext *ctx, JSValue n) {
	host_set_func(ctx, n, "readText", js_read_text, 1);
	host_set_func(ctx, n, "readBinary", js_read_binary, 1);
	host_set_func(ctx, n, "storageLoad", js_storage_load, 0);
	host_set_func(ctx, n, "storageSave", js_storage_save, 1);
	host_set_func(ctx, n, "now", js_now, 0);
	host_set_func(ctx, n, "imageInfo", js_image_info, 1);
	host_set_func(ctx, n, "surfCreate", js_surf_create, 2);
	host_set_func(ctx, n, "surfResize", js_surf_resize, 3);
	host_set_func(ctx, n, "surfFree", js_surf_free, 1);
	host_set_func(ctx, n, "surfClear", js_surf_clear, 5);
	host_set_func(ctx, n, "surfFill", js_surf_fill, 9);
	host_set_func(ctx, n, "surfDraw", js_surf_draw, 12);
	host_set_func(ctx, n, "surfGet", js_surf_get, 5);
	host_set_func(ctx, n, "surfText", js_surf_text, 12);
	host_set_func(ctx, n, "textWidth", js_text_width, 2);
	JS_SetPropertyStr(ctx, n, "lang", JS_NewString(ctx, plat_lang()));
	JS_SetPropertyStr(ctx, n, "screenW", JS_NewInt32(ctx, SCREEN_W));
	JS_SetPropertyStr(ctx, n, "screenH", JS_NewInt32(ctx, SCREEN_H));
}
