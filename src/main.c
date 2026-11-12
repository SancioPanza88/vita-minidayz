/*
 * Mini DayZ for PS Vita - native host.
 *
 * Runs the unmodified Construct 2 export (c2runtime.js + data.js) inside
 * QuickJS. The runtime is started in its "Ejecta" mode (a DOM-less native
 * environment), and src/js/prelude.js provides the small browser surface it
 * still needs, backed by the native functions in natives.c / gl.c / audio.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "host.h"
#include "input.h"
#ifdef MDZ_PROFILE
#include <time.h>
#endif

JSContext *g_ctx;
static JSRuntime *g_rt;

void host_logf(const char *fmt, ...) {
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	plat_log(buf);
}

void host_set_func(JSContext *ctx, JSValue obj, const char *name, JSCFunction *fn, int argc) {
	JS_SetPropertyStr(ctx, obj, name, JS_NewCFunction(ctx, fn, name, argc));
}

void *host_read_file(const char *path, size_t *size) {
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc(sz + 1);
	if (!buf) { fclose(f); return NULL; }
	if (sz > 0 && fread(buf, 1, sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
	fclose(f);
	buf[sz] = 0;
	if (size) *size = sz;
	return buf;
}

char *host_game_path(const char *rel) {
	const char *base = plat_game_dir();
	while (rel[0] == '.' && rel[1] == '/') rel += 2;
	size_t n = strlen(base) + strlen(rel) + 1;
	char *p = malloc(n);
	snprintf(p, n, "%s%s", base, rel);
	/* strip query strings ("file.png?123") */
	char *q = strchr(p + strlen(base), '?');
	if (q) *q = 0;
	return p;
}

static void dump_exception(JSContext *ctx) {
	JSValue ex = JS_GetException(ctx);
	const char *s = JS_ToCString(ctx, ex);
	host_logf("JS exception: %s", s ? s : "?");
	if (s) JS_FreeCString(ctx, s);
	if (JS_IsObject(ex)) {
		JSValue st = JS_GetPropertyStr(ctx, ex, "stack");
		if (!JS_IsUndefined(st)) {
			const char *ss = JS_ToCString(ctx, st);
			if (ss) { host_logf("%s", ss); JS_FreeCString(ctx, ss); }
		}
		JS_FreeValue(ctx, st);
	}
	JS_FreeValue(ctx, ex);
}

static int eval_file(const char *path, const char *name) {
	size_t sz;
	char *src = host_read_file(path, &sz);
	if (!src) { host_logf("cannot read %s", path); return -1; }
	char *code = src;
	if ((unsigned char)code[0] == 0xEF && (unsigned char)code[1] == 0xBB && (unsigned char)code[2] == 0xBF) { code += 3; sz -= 3; }
	JSValue r = JS_Eval(g_ctx, code, sz, name, JS_EVAL_TYPE_GLOBAL);
	free(src);
	if (JS_IsException(r)) { dump_exception(g_ctx); return -1; }
	JS_FreeValue(g_ctx, r);
	return 0;
}

/* QuickJS lets the heap grow to 150% of the live size before collecting; with a
 * ~150 MB live heap that does not fit in the Vita's RAM. Keep the headroom fixed. */
#define GC_HEADROOM (24 * 1024 * 1024)
static void clamp_gc_threshold(void) {
	static size_t ours;
	size_t t = JS_GetGCThreshold(g_rt);
	if (t == ours) return;
	size_t live = t / 3 * 2; /* threshold was just set to live * 1.5 by a collection */
	ours = live + GC_HEADROOM;
	if (ours > t) ours = t;
	JS_SetGCThreshold(g_rt, ours);
}

static void run_jobs(void) {
	JSContext *c;
	int r;
	while ((r = JS_ExecutePendingJob(g_rt, &c)) != 0) {
		if (r < 0) dump_exception(c);
	}
}

/* calls global function `name` with numeric args */
int host_call(const char *name, int argc, double *args) {
	int ok = 1;
	JSValue g = JS_GetGlobalObject(g_ctx);
	JSValue f = JS_GetPropertyStr(g_ctx, g, name);
	if (JS_IsFunction(g_ctx, f)) {
		JSValue av[8];
		for (int i = 0; i < argc && i < 8; i++) av[i] = JS_NewFloat64(g_ctx, args[i]);
		JSValue r = JS_Call(g_ctx, f, g, argc, av);
		if (JS_IsException(r)) { dump_exception(g_ctx); ok = 0; }
		JS_FreeValue(g_ctx, r);
	}
	JS_FreeValue(g_ctx, f);
	JS_FreeValue(g_ctx, g);
	return ok;
}

/* evalScript(source, name): global-scope evaluation (used to load the patched runtime) */
static JSValue js_eval_script(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	size_t len;
	const char *src = JS_ToCStringLen(ctx, &len, argv[0]);
	const char *name = JS_ToCString(ctx, argv[1]);
	JSValue r = JS_Eval(ctx, src, len, name ? name : "script", JS_EVAL_TYPE_GLOBAL);
	JS_FreeCString(ctx, src);
	if (name) JS_FreeCString(ctx, name);
	return r;
}

/* memInfo() -> JS heap bytes in use */
static JSValue js_mem_info(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	JSMemoryUsage u;
	if (argc > 0 && JS_ToBool(ctx, argv[0])) JS_RunGC(g_rt);
	JS_ComputeMemoryUsage(g_rt, &u);
#ifndef __vita__
	if (argc > 1) JS_DumpMemoryUsage(stderr, &u, g_rt);
#endif
	return JS_NewFloat64(ctx, (double)u.malloc_size);
}

static JSValue js_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	const char *s = JS_ToCString(ctx, argv[0]);
	if (s) { plat_log(s); JS_FreeCString(ctx, s); }
	return JS_UNDEFINED;
}

int main(int argc, char **argv) {
	if (plat_init() != 0) return 1;
	host_logf("Mini DayZ Vita host starting");

	g_rt = JS_NewRuntime();
	JS_SetMemoryLimit(g_rt, 0);
	JS_SetMaxStackSize(g_rt, 0); /* stack is checked by the OS thread size */
	JS_SetGCThreshold(g_rt, 32 * 1024 * 1024);
	g_ctx = JS_NewContext(g_rt);

	JSValue global = JS_GetGlobalObject(g_ctx);
	JSValue n = JS_NewObject(g_ctx);
	host_set_func(g_ctx, n, "log", js_log, 1);
	host_set_func(g_ctx, n, "evalScript", js_eval_script, 2);
	host_set_func(g_ctx, n, "memInfo", js_mem_info, 0);
	natives_register(g_ctx, n);
	gl_register(g_ctx, n);
	audio_register(g_ctx, n);
	JS_SetPropertyStr(g_ctx, global, "__n", n);
	JS_FreeValue(g_ctx, global);

	audio_init();
	input_load_config();

	char path[512];
	const char *scripts[] = { "glconst.js", "xml.js", "prelude.js", NULL };
	for (int i = 0; scripts[i]; i++) {
		snprintf(path, sizeof(path), "%s%s", plat_res_dir(), scripts[i]);
		if (eval_file(path, scripts[i]) != 0) {
			char msg[600];
			snprintf(msg, sizeof(msg), "%s non caricato / failed to load", path);
			plat_fatal(msg);
		}
	}
	if (!host_call("__boot", 0, NULL)) plat_fatal("avvio del gioco fallito / game startup failed (c2runtime.js)");
	run_jobs();

	InputState in;
	memset(&in, 0, sizeof(in));
	for (int frame = 0;; frame++) {
		plat_poll(&in, frame);
		if (in.quit) break;
		input_process(&in);
		double now = plat_now_ms();
#ifdef MDZ_PROFILE
		struct timespec a, b;
		clock_gettime(CLOCK_MONOTONIC, &a);
#endif
		host_call("__frame", 1, &now);
		run_jobs();
		clamp_gc_threshold();
#ifdef MDZ_PROFILE
		clock_gettime(CLOCK_MONOTONIC, &b);
		static double js_acc; static int js_n;
		js_acc += (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1e6; js_n++;
		if (js_n == 120) { host_logf("[profile] JS %.2f ms/frame", js_acc / js_n); js_acc = 0; js_n = 0; }
#endif
		overlay_draw();
		plat_swap(frame);
	}
	plat_shutdown();
	return 0;
}
