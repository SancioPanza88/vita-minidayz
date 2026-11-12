/* Thin WebGL 1 -> OpenGL ES 2 (vitaGL on Vita) binding. Object handles are plain ints. */
#include <stdlib.h>
#include <string.h>
#include "host.h"
#include "glinc.h"

static int unpack_premul, unpack_flip;

static double argd(JSContext *ctx, JSValueConst v) { double d = 0; JS_ToFloat64(ctx, &d, v); return d; }
static int argi(JSContext *ctx, JSValueConst v) { int i = 0; JS_ToInt32(ctx, &i, v); return i; }
#define I(n) argi(ctx, argv[n])
#define D(n) argd(ctx, argv[n])
#define F(n) ((float)argd(ctx, argv[n]))

/* bytes of an ArrayBuffer or typed array */
static uint8_t *get_bytes(JSContext *ctx, JSValueConst v, size_t *size) {
	size_t off = 0, len = 0, bpe = 0;
	*size = 0;
	if (!JS_IsObject(v)) return NULL;
	JSValue ab = JS_GetTypedArrayBuffer(ctx, v, &off, &len, &bpe);
	if (!JS_IsException(ab)) {
		size_t abs;
		uint8_t *p = JS_GetArrayBuffer(ctx, &abs, ab);
		JS_FreeValue(ctx, ab);
		if (!p) return NULL;
		*size = len;
		return p + off;
	}
	JS_FreeValue(ctx, JS_GetException(ctx));
	uint8_t *p = JS_GetArrayBuffer(ctx, size, v);
	if (!p) JS_FreeValue(ctx, JS_GetException(ctx));
	return p;
}

#define V0(name, call) static JSValue js_##name(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { call; return JS_UNDEFINED; }

V0(activeTexture, glActiveTexture(I(0)))
V0(attachShader, glAttachShader(I(0), I(1)))
V0(bindBuffer, glBindBuffer(I(0), I(1)))
V0(bindFramebuffer, glBindFramebuffer(I(0), I(1)))
V0(bindRenderbuffer, glBindRenderbuffer(I(0), I(1)))
V0(bindTexture, glBindTexture(I(0), I(1)))
V0(blendFunc, glBlendFunc(I(0), I(1)))
V0(blendFuncSeparate, glBlendFuncSeparate(I(0), I(1), I(2), I(3)))
V0(blendEquation, glBlendEquation(I(0)))
V0(clear, glClear(I(0)))
V0(clearColor, glClearColor(F(0), F(1), F(2), F(3)))
V0(clearDepth, glClearDepthf(F(0)))
V0(colorMask, glColorMask(I(0), I(1), I(2), I(3)))
V0(deleteProgram, glDeleteProgram(I(0)))
V0(deleteShader, glDeleteShader(I(0)))
V0(depthFunc, glDepthFunc(I(0)))
V0(depthMask, glDepthMask(I(0)))
V0(disable, glDisable(I(0)))
V0(enable, glEnable(I(0)))
V0(disableVertexAttribArray, glDisableVertexAttribArray(I(0)))
V0(enableVertexAttribArray, glEnableVertexAttribArray(I(0)))
V0(drawArrays, glDrawArrays(I(0), I(1), I(2)))
V0(drawElements, glDrawElements(I(0), I(1), I(2), (const void *)(intptr_t)I(3)))
V0(framebufferTexture2D, glFramebufferTexture2D(I(0), I(1), I(2), I(3), I(4)))
V0(framebufferRenderbuffer, glFramebufferRenderbuffer(I(0), I(1), I(2), I(3)))
V0(renderbufferStorage, glRenderbufferStorage(I(0), I(1), I(2), I(3)))
V0(generateMipmap, glGenerateMipmap(I(0)))
V0(linkProgram, glLinkProgram(I(0)))
V0(scissor, glScissor(I(0), I(1), I(2), I(3)))
V0(texParameteri, glTexParameteri(I(0), I(1), I(2)))
V0(texParameterf, glTexParameterf(I(0), I(1), F(2)))
V0(uniform1f, glUniform1f(I(0), F(1)))
V0(uniform2f, glUniform2f(I(0), F(1), F(2)))
V0(uniform3f, glUniform3f(I(0), F(1), F(2), F(3)))
V0(uniform4f, glUniform4f(I(0), F(1), F(2), F(3), F(4)))
V0(uniform1i, glUniform1i(I(0), I(1)))
V0(useProgram, glUseProgram(I(0)))
V0(vertexAttribPointer, glVertexAttribPointer(I(0), I(1), I(2), I(3), I(4), (const void *)(intptr_t)I(5)))
V0(viewport, glViewport(I(0), I(1), I(2), I(3)))
V0(flush, glFlush())
V0(finish, glFinish())
V0(lineWidth, glLineWidth(F(0)))

static JSValue js_pixelStorei(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	int p = I(0), v = I(1);
	if (p == 0x9241) unpack_premul = v;
	else if (p == 0x9240) unpack_flip = v;
	else if (p == 0x9243) { /* colorspace conversion: ignore */ }
	else glPixelStorei(p, v);
	return JS_UNDEFINED;
}

#define GEN(name, fn) static JSValue js_##name(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { GLuint id = 0; fn(1, &id); return JS_NewInt32(ctx, id); }
GEN(createBuffer, glGenBuffers)
GEN(createTexture, glGenTextures)
GEN(createFramebuffer, glGenFramebuffers)
GEN(createRenderbuffer, glGenRenderbuffers)
#define DEL(name, fn) static JSValue js_##name(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { GLuint id = I(0); if (id) fn(1, &id); return JS_UNDEFINED; }
DEL(deleteBuffer, glDeleteBuffers)
DEL(deleteTexture, glDeleteTextures)
DEL(deleteFramebuffer, glDeleteFramebuffers)
DEL(deleteRenderbuffer, glDeleteRenderbuffers)

static JSValue js_compileShader(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	double a = plat_now_ms();
	glCompileShader(I(0));
	host_logf("[gl] shader %d compiled in %d ms", I(0), (int)(plat_now_ms() - a));
	return JS_UNDEFINED;
}
static JSValue js_createProgram(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { return JS_NewInt32(ctx, glCreateProgram()); }
static JSValue js_createShader(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { return JS_NewInt32(ctx, glCreateShader(I(0))); }
static JSValue js_getError(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { return JS_NewInt32(ctx, glGetError()); }
static JSValue js_checkFramebufferStatus(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { return JS_NewInt32(ctx, glCheckFramebufferStatus(I(0))); }

static JSValue js_shaderSource(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	const char *s = JS_ToCString(ctx, argv[1]);
	if (!s) return JS_UNDEFINED;
	const char *srcs[1] = { s };
	glShaderSource(I(0), 1, srcs, NULL);
	JS_FreeCString(ctx, s);
	return JS_UNDEFINED;
}

static JSValue js_getShaderParameter(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	GLint v = GL_TRUE; /* vitaGL leaves the value untouched (always succeeds or aborts) */
	glGetShaderiv(I(0), I(1), &v);
	return JS_NewInt32(ctx, v);
}
static JSValue js_getProgramParameter(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	GLint v = GL_TRUE;
	glGetProgramiv(I(0), I(1), &v);
	return JS_NewInt32(ctx, v);
}
static JSValue js_getShaderInfoLog(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	char buf[2048] = { 0 };
	GLsizei len = 0;
	glGetShaderInfoLog(I(0), sizeof(buf) - 1, &len, buf);
	return JS_NewString(ctx, buf);
}
static JSValue js_getProgramInfoLog(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	char buf[2048] = { 0 };
	GLsizei len = 0;
	glGetProgramInfoLog(I(0), sizeof(buf) - 1, &len, buf);
	return JS_NewString(ctx, buf);
}
static JSValue js_getAttribLocation(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	const char *s = JS_ToCString(ctx, argv[1]);
	int r = s ? glGetAttribLocation(I(0), s) : -1;
	if (s) JS_FreeCString(ctx, s);
	return JS_NewInt32(ctx, r);
}
static JSValue js_getUniformLocation(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	const char *s = JS_ToCString(ctx, argv[1]);
	int r = s ? glGetUniformLocation(I(0), s) : -1;
	if (s) JS_FreeCString(ctx, s);
	return JS_NewInt32(ctx, r);
}

static JSValue js_getParameter(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	int p = I(0);
	switch (p) {
	case GL_VERSION: return JS_NewString(ctx, "WebGL 1.0 (OpenGL ES 2.0)");
	case GL_SHADING_LANGUAGE_VERSION: return JS_NewString(ctx, "WebGL GLSL ES 1.0");
	case GL_VENDOR: case 0x9245: return JS_NewString(ctx, "vita-minidayz");
	case GL_RENDERER: case 0x9246: return JS_NewString(ctx, "GLES2");
	case GL_ALIASED_POINT_SIZE_RANGE: case GL_ALIASED_LINE_WIDTH_RANGE: case GL_VIEWPORT: case GL_MAX_VIEWPORT_DIMS: {
		GLint v[4] = { 1, 64, 0, 0 };
		if (p == GL_VIEWPORT || p == GL_MAX_VIEWPORT_DIMS) glGetIntegerv(p, v);
		JSValue a = JS_NewArray(ctx);
		int n = (p == GL_VIEWPORT) ? 4 : 2;
		for (int i = 0; i < n; i++) JS_SetPropertyUint32(ctx, a, i, JS_NewInt32(ctx, v[i]));
		return a;
	}
	default: {
		GLint v = 0;
		glGetIntegerv(p, &v);
		if (p == GL_MAX_TEXTURE_SIZE && v <= 0) v = 4096;
		return JS_NewInt32(ctx, v);
	}
	}
}

static JSValue js_bufferData(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	if (JS_IsNumber(argv[1])) {
		glBufferData(I(0), I(1), NULL, I(2));
	} else {
		size_t sz;
		uint8_t *p = get_bytes(ctx, argv[1], &sz);
		glBufferData(I(0), sz, p, I(2));
	}
	return JS_UNDEFINED;
}
static JSValue js_bufferSubData(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	size_t sz;
	uint8_t *p = get_bytes(ctx, argv[2], &sz);
	if (p && sz) glBufferSubData(I(0), I(1), sz, p);
	return JS_UNDEFINED;
}
static JSValue js_uniformMatrix4fv(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	size_t sz;
	uint8_t *p = get_bytes(ctx, argv[2], &sz);
	if (p && sz >= 64) glUniformMatrix4fv(I(0), (GLsizei)(sz / 64), I(1), (const GLfloat *)p);
	return JS_UNDEFINED;
}
static JSValue js_uniform1fv(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	size_t sz;
	uint8_t *p = get_bytes(ctx, argv[1], &sz);
	if (p && sz >= 4) glUniform1fv(I(0), (GLsizei)(sz / 4), (const GLfloat *)p);
	return JS_UNDEFINED;
}

static void prepare_pixels(uint8_t *px, int w, int h) {
	if (unpack_premul) {
		size_t n = (size_t)w * h;
		for (size_t i = 0; i < n; i++) {
			uint8_t *p = px + i * 4;
			int a = p[3];
			if (a != 255) {
				p[0] = p[0] * a / 255;
				p[1] = p[1] * a / 255;
				p[2] = p[2] * a / 255;
			}
		}
	}
	if (unpack_flip) {
		size_t row = (size_t)w * 4;
		uint8_t *tmp = malloc(row);
		for (int y = 0; y < h / 2; y++) {
			memcpy(tmp, px + y * row, row);
			memcpy(px + y * row, px + (h - 1 - y) * row, row);
			memcpy(px + (h - 1 - y) * row, tmp, row);
		}
		free(tmp);
	}
}

/* texImage2D(target, level, ifmt, fmt, type, kind(0=image path,1=surface), src) */
static JSValue js_texImage2DSource(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	int kind = I(5);
	uint8_t *px = NULL;
	int w = 0, h = 0;
	if (kind == 0) {
		const char *rel = JS_ToCString(ctx, argv[6]);
		if (rel) { px = image_decode(rel, &w, &h); JS_FreeCString(ctx, rel); }
		if (!px) { /* missing image: upload 1x1 transparent so rendering keeps going */
			px = calloc(1, 4); w = h = 1;
		}
	} else {
		Surface *s = surface_get(I(6));
		if (!s) return JS_UNDEFINED;
		w = s->w; h = s->h;
		px = malloc((size_t)w * h * 4);
		memcpy(px, s->px, (size_t)w * h * 4);
	}
	prepare_pixels(px, w, h);
	glTexImage2D(I(0), I(1), GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	free(px);
	return JS_NewInt32(ctx, w | (h << 16));
}

/* texImage2D(target, level, ifmt, w, h, border, fmt, type, data|null) */
static JSValue js_texImage2DData(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	size_t sz = 0;
	uint8_t *p = JS_IsNull(argv[8]) || JS_IsUndefined(argv[8]) ? NULL : get_bytes(ctx, argv[8], &sz);
	int w = I(3), h = I(4);
	if (p && (unpack_premul || unpack_flip) && sz >= (size_t)w * h * 4) {
		uint8_t *cp = malloc((size_t)w * h * 4);
		memcpy(cp, p, (size_t)w * h * 4);
		prepare_pixels(cp, w, h);
		glTexImage2D(I(0), I(1), I(2), w, h, I(5), I(6), I(7), cp);
		free(cp);
	} else {
		glTexImage2D(I(0), I(1), I(2), w, h, I(5), I(6), I(7), p);
	}
	return JS_UNDEFINED;
}

/* texSubImage2D(target, level, x, y, fmt, type, kind, src) - source variant */
static JSValue js_texSubImage2DSource(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	int kind = I(6);
	uint8_t *px = NULL;
	int w = 0, h = 0;
	if (kind == 0) {
		const char *rel = JS_ToCString(ctx, argv[7]);
		if (rel) { px = image_decode(rel, &w, &h); JS_FreeCString(ctx, rel); }
	} else {
		Surface *s = surface_get(I(7));
		if (s) { w = s->w; h = s->h; px = malloc((size_t)w * h * 4); memcpy(px, s->px, (size_t)w * h * 4); }
	}
	if (!px) return JS_UNDEFINED;
	prepare_pixels(px, w, h);
	glTexSubImage2D(I(0), I(1), I(2), I(3), w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
	free(px);
	return JS_UNDEFINED;
}

static JSValue js_readPixels(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	size_t sz;
	uint8_t *p = get_bytes(ctx, argv[6], &sz);
	if (p && sz >= (size_t)I(2) * I(3) * 4) glReadPixels(I(0), I(1), I(2), I(3), I(4), I(5), p);
	return JS_UNDEFINED;
}

#define R(name, n) host_set_func(ctx, gl, #name, js_##name, n)
void gl_register(JSContext *ctx, JSValue n) {
	JSValue gl = JS_NewObject(ctx);
	R(activeTexture, 1); R(attachShader, 2); R(bindBuffer, 2); R(bindFramebuffer, 2);
	R(bindRenderbuffer, 2); R(bindTexture, 2); R(blendFunc, 2); R(blendFuncSeparate, 4);
	R(blendEquation, 1); R(clear, 1); R(clearColor, 4); R(clearDepth, 1); R(colorMask, 4);
	R(compileShader, 1); R(deleteProgram, 1); R(deleteShader, 1); R(depthFunc, 1);
	R(depthMask, 1); R(disable, 1); R(enable, 1); R(disableVertexAttribArray, 1);
	R(enableVertexAttribArray, 1); R(drawArrays, 3); R(drawElements, 4);
	R(framebufferTexture2D, 5); R(framebufferRenderbuffer, 4); R(renderbufferStorage, 4);
	R(generateMipmap, 1); R(linkProgram, 1); R(scissor, 4); R(texParameteri, 3);
	R(texParameterf, 3); R(uniform1f, 2); R(uniform2f, 3); R(uniform3f, 4); R(uniform4f, 5);
	R(uniform1i, 2); R(useProgram, 1); R(vertexAttribPointer, 6); R(viewport, 4);
	R(flush, 0); R(finish, 0); R(lineWidth, 1); R(pixelStorei, 2);
	R(createBuffer, 0); R(createTexture, 0); R(createFramebuffer, 0); R(createRenderbuffer, 0);
	R(deleteBuffer, 1); R(deleteTexture, 1); R(deleteFramebuffer, 1); R(deleteRenderbuffer, 1);
	R(createProgram, 0); R(createShader, 1); R(getError, 0); R(checkFramebufferStatus, 1);
	R(shaderSource, 2); R(getShaderParameter, 2); R(getProgramParameter, 2);
	R(getShaderInfoLog, 1); R(getProgramInfoLog, 1); R(getAttribLocation, 2);
	R(getUniformLocation, 2); R(getParameter, 1); R(bufferData, 3); R(bufferSubData, 3);
	R(uniformMatrix4fv, 3); R(uniform1fv, 2); R(texImage2DSource, 7); R(texImage2DData, 9);
	R(texSubImage2DSource, 8); R(readPixels, 7);
	JS_SetPropertyStr(ctx, n, "gl", gl);
}
