/* Minimal audio engine backing the Web Audio / HTMLAudioElement emulation.
 * Sounds are Ogg Vorbis. Short sounds are decoded once; long ones are streamed
 * from the compressed data by each voice. Output: 48 kHz stereo s16. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "host.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define OUT_RATE 48000
#define MAX_BUF 1024
#define MAX_VOICE 48
#define STREAM_THRESHOLD (384 * 1024)      /* decoded bytes above which a sound is streamed */
#define DECODED_BUDGET (24 * 1024 * 1024)    /* max total memory for fully decoded sounds */
static size_t decoded_total;

typedef struct {
	int used, refs;
	int channels, rate;
	int frames;
	int16_t *pcm;          /* decoded, or NULL if streamed */
	size_t pcm_bytes;
	uint8_t *ogg; int ogg_size;
} ABuf;

typedef struct {
	int active, paused, loop, ended_flag;
	int buf;
	double pos, step;      /* in source frames */
	float gain, pan, rate;
	stb_vorbis *vb;        /* streaming decoder */
	int16_t sbuf[4096 * 2];
	int sbuf_len, sbuf_pos; /* frames in sbuf, current read index */
	int sbuf_start;         /* source frame index of sbuf[0] */
	int16_t last[2];
} Voice;

static ABuf bufs[MAX_BUF];
static Voice voices[MAX_VOICE];
static int voice_serial[MAX_VOICE];

static void buf_unref(int id) {
	ABuf *b = &bufs[id];
	if (--b->refs <= 0) {
		decoded_total -= b->pcm_bytes;
		free(b->pcm); free(b->ogg);
		memset(b, 0, sizeof(*b));
	}
}

static int stream_fill(Voice *v, ABuf *b) {
	int ch = b->channels;
	float **out;
	v->sbuf_start += v->sbuf_len;
	v->sbuf_len = 0; v->sbuf_pos = 0;
	int n = stb_vorbis_get_frame_float(v->vb, NULL, &out);
	if (n <= 0) return 0;
	if (n > 4096) n = 4096;
	for (int i = 0; i < n; i++) {
		float l = out[0][i], r = ch > 1 ? out[1][i] : l;
		int L = (int)(l * 32767.f), R = (int)(r * 32767.f);
		v->sbuf[i * 2] = L > 32767 ? 32767 : L < -32768 ? -32768 : L;
		v->sbuf[i * 2 + 1] = R > 32767 ? 32767 : R < -32768 ? -32768 : R;
	}
	v->sbuf_len = n;
	return n;
}

static void mix(int16_t *out, int frames) {
	static int32_t acc[4096 * 2];
	if (frames > 4096) frames = 4096;
	memset(acc, 0, sizeof(int32_t) * frames * 2);
	plat_audio_lock();
	for (int vi = 0; vi < MAX_VOICE; vi++) {
		Voice *v = &voices[vi];
		if (!v->active || v->paused) continue;
		ABuf *b = &bufs[v->buf];
		float gl = v->gain * (v->pan > 0 ? 1.f - v->pan : 1.f);
		float gr = v->gain * (v->pan < 0 ? 1.f + v->pan : 1.f);
		int il = (int)(gl * 256.f), ir = (int)(gr * 256.f);
		for (int i = 0; i < frames; i++) {
			int idx = (int)v->pos;
			int L, R;
			if (b->pcm) {
				if (idx >= b->frames) {
					if (v->loop && b->frames > 0) { v->pos -= b->frames; idx = (int)v->pos; if (idx >= b->frames) idx = v->pos = 0; }
					else { v->active = 0; v->ended_flag = 1; break; }
				}
				if (b->channels == 1) L = R = b->pcm[idx];
				else { L = b->pcm[idx * 2]; R = b->pcm[idx * 2 + 1]; }
			} else {
				while (idx >= v->sbuf_start + v->sbuf_len) {
					if (!stream_fill(v, b)) {
						if (v->loop) {
							stb_vorbis_seek_start(v->vb);
							v->pos -= v->sbuf_start; v->sbuf_start = 0; v->sbuf_len = 0;
							idx = (int)v->pos;
							if (!stream_fill(v, b)) { v->active = 0; v->ended_flag = 1; break; }
							v->sbuf_start = 0;
						} else { v->active = 0; v->ended_flag = 1; break; }
					}
				}
				if (!v->active) break;
				int k = idx - v->sbuf_start;
				if (k < 0) k = 0;
				L = v->sbuf[k * 2]; R = v->sbuf[k * 2 + 1];
			}
			acc[i * 2] += (L * il) >> 8;
			acc[i * 2 + 1] += (R * ir) >> 8;
			v->pos += v->step;
		}
	}
	plat_audio_unlock();
	for (int i = 0; i < frames * 2; i++) {
		int s = acc[i];
		out[i] = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
	}
}

/* audDecode(arraybuffer) -> buffer id (>0) or -1 */
static JSValue js_decode(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	size_t sz;
	uint8_t *p = JS_GetArrayBuffer(ctx, &sz, argv[0]);
	if (!p) {
		JS_FreeValue(ctx, JS_GetException(ctx));
		size_t off, len, bpe;
		JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[0], &off, &len, &bpe);
		if (JS_IsException(ab)) { JS_FreeValue(ctx, JS_GetException(ctx)); return JS_NewInt32(ctx, -1); }
		p = JS_GetArrayBuffer(ctx, &sz, ab);
		JS_FreeValue(ctx, ab);
		if (!p) return JS_NewInt32(ctx, -1);
		p += off; sz = len;
	}
	int err;
	stb_vorbis *vb = stb_vorbis_open_memory(p, (int)sz, &err, NULL);
	if (!vb) { host_logf("ogg decode failed (%d)", err); return JS_NewInt32(ctx, -1); }
	stb_vorbis_info info = stb_vorbis_get_info(vb);
	int frames = (int)stb_vorbis_stream_length_in_samples(vb);
	int id = -1;
	plat_audio_lock();
	for (int i = 1; i < MAX_BUF; i++) if (!bufs[i].used) { id = i; bufs[i].used = 1; break; }
	plat_audio_unlock();
	if (id < 0) { stb_vorbis_close(vb); host_logf("out of audio buffers"); return JS_NewInt32(ctx, -1); }
	ABuf b = { 0 };
	b.used = 1; b.refs = 1;
	b.channels = info.channels > 1 ? 2 : 1;
	b.rate = info.sample_rate > 0 ? info.sample_rate : 44100;
	int known = frames > 0 && frames < 0x7FFFFFF;
	b.frames = known ? frames : b.rate * 60; /* unknown length: assume one minute, ends at EOF */
	size_t need = known ? (size_t)frames * b.channels * 2 : 0;
	if (known && need <= STREAM_THRESHOLD && decoded_total + need <= DECODED_BUDGET && info.channels <= 2) {
		b.pcm = malloc(need + b.channels * 2);
		if (b.pcm) {
			b.frames = stb_vorbis_get_samples_short_interleaved(vb, b.channels, b.pcm, frames * b.channels);
			decoded_total += need;
			b.pcm_bytes = need;
		}
	}
	if (!b.pcm) {
		b.ogg = malloc(sz);
		if (!b.ogg) {
			stb_vorbis_close(vb);
			host_logf("audio: out of memory (%u bytes)", (unsigned)sz);
			plat_audio_lock(); bufs[id].used = 0; plat_audio_unlock();
			return JS_NewInt32(ctx, -1);
		}
		memcpy(b.ogg, p, sz);
		b.ogg_size = (int)sz;
	}
	stb_vorbis_close(vb);
	plat_audio_lock();
	bufs[id] = b;
	plat_audio_unlock();
	return JS_NewInt32(ctx, id);
}

static JSValue js_info(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	int id = 0; JS_ToInt32(ctx, &id, argv[0]);
	if (id <= 0 || id >= MAX_BUF || !bufs[id].used) return JS_NULL;
	JSValue a = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, a, 0, JS_NewInt32(ctx, bufs[id].frames));
	JS_SetPropertyUint32(ctx, a, 1, JS_NewInt32(ctx, bufs[id].rate));
	JS_SetPropertyUint32(ctx, a, 2, JS_NewInt32(ctx, bufs[id].channels));
	return a;
}

static JSValue js_buf_free(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	int id = 0; JS_ToInt32(ctx, &id, argv[0]);
	if (id <= 0 || id >= MAX_BUF || !bufs[id].used) return JS_UNDEFINED;
	plat_audio_lock();
	buf_unref(id);
	plat_audio_unlock();
	return JS_UNDEFINED;
}

static void voice_release(Voice *v) {
	if (v->vb) { stb_vorbis_close(v->vb); v->vb = NULL; }
	if (v->buf) buf_unref(v->buf);
	v->buf = 0;
	v->active = 0;
}

/* voice handle = slot | serial << 8 */
static Voice *voice_from(JSContext *ctx, JSValueConst h) {
	int id = 0; JS_ToInt32(ctx, &id, h);
	int slot = id & 0xFF;
	if (slot < 0 || slot >= MAX_VOICE || voice_serial[slot] != (id >> 8)) return NULL;
	return &voices[slot];
}

/* audPlay(buf, loop, gain, rate, offsetSeconds, pan) -> voice handle or -1 */
static JSValue js_play(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	int id = 0, loop = 0;
	double gain = 1, rate = 1, off = 0, pan = 0;
	JS_ToInt32(ctx, &id, argv[0]); JS_ToInt32(ctx, &loop, argv[1]);
	JS_ToFloat64(ctx, &gain, argv[2]); JS_ToFloat64(ctx, &rate, argv[3]);
	JS_ToFloat64(ctx, &off, argv[4]);
	if (argc > 5) JS_ToFloat64(ctx, &pan, argv[5]);
	if (id <= 0 || id >= MAX_BUF || !bufs[id].used) return JS_NewInt32(ctx, -1);
	plat_audio_lock();
	int slot = -1;
	for (int i = 0; i < MAX_VOICE; i++) if (!voices[i].active && !voices[i].ended_flag && !voices[i].buf) { slot = i; break; }
	if (slot < 0) { /* steal a finished-but-unreported or the oldest voice */
		for (int i = 0; i < MAX_VOICE; i++) if (!voices[i].active) { slot = i; break; }
	}
	if (slot < 0) { plat_audio_unlock(); return JS_NewInt32(ctx, -1); }
	Voice *v = &voices[slot];
	voice_release(v);
	memset(v, 0, sizeof(*v));
	ABuf *b = &bufs[id];
	b->refs++;
	v->buf = id;
	v->loop = loop;
	v->gain = (float)gain;
	v->pan = (float)pan;
	v->rate = (float)rate;
	v->step = (double)b->rate / OUT_RATE * (rate > 0 ? rate : 1);
	if (!b->pcm) {
		int err;
		v->vb = stb_vorbis_open_memory(b->ogg, b->ogg_size, &err, NULL);
		if (!v->vb) { voice_release(v); plat_audio_unlock(); return JS_NewInt32(ctx, -1); }
		if (off > 0) {
			unsigned int f = (unsigned int)(off * b->rate);
			if (f < (unsigned)b->frames && stb_vorbis_seek(v->vb, f)) { v->sbuf_start = f; v->pos = f; }
		}
	} else if (off > 0) {
		v->pos = off * b->rate;
	}
	v->active = 1;
	voice_serial[slot] = (voice_serial[slot] + 1) & 0x7FFFFF;
	int handle = slot | (voice_serial[slot] << 8);
	plat_audio_unlock();
	return JS_NewInt32(ctx, handle);
}

static JSValue js_stop(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	plat_audio_lock();
	Voice *v = voice_from(ctx, argv[0]);
	if (v) { voice_release(v); v->ended_flag = 0; voice_serial[v - voices] = (voice_serial[v - voices] + 1) & 0x7FFFFF; }
	plat_audio_unlock();
	return JS_UNDEFINED;
}

/* audSet(voice, gain, rate, pan, paused, loop) */
static JSValue js_set(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	double gain, rate, pan; int paused, loop;
	JS_ToFloat64(ctx, &gain, argv[1]); JS_ToFloat64(ctx, &rate, argv[2]); JS_ToFloat64(ctx, &pan, argv[3]);
	JS_ToInt32(ctx, &paused, argv[4]); JS_ToInt32(ctx, &loop, argv[5]);
	plat_audio_lock();
	Voice *v = voice_from(ctx, argv[0]);
	if (v && v->buf) {
		v->gain = (float)gain; v->pan = (float)pan; v->paused = paused; v->loop = loop;
		v->step = (double)bufs[v->buf].rate / OUT_RATE * (rate > 0 ? rate : 1);
	}
	plat_audio_unlock();
	return JS_UNDEFINED;
}

static JSValue js_pos(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	double r = 0;
	plat_audio_lock();
	Voice *v = voice_from(ctx, argv[0]);
	if (v && v->buf) r = v->pos / bufs[v->buf].rate;
	plat_audio_unlock();
	return JS_NewFloat64(ctx, r);
}

/* returns array of voice handles that finished since last call */
static JSValue js_ended(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
	JSValue a = JS_NewArray(ctx);
	int n = 0;
	plat_audio_lock();
	for (int i = 0; i < MAX_VOICE; i++) {
		if (voices[i].ended_flag) {
			JS_SetPropertyUint32(ctx, a, n++, JS_NewInt32(ctx, i | (voice_serial[i] << 8)));
			voices[i].ended_flag = 0;
			voice_release(&voices[i]);
		}
	}
	plat_audio_unlock();
	return a;
}

void audio_init(void) {
	plat_audio_start(mix);
}

void audio_register(JSContext *ctx, JSValue n) {
	host_set_func(ctx, n, "audDecode", js_decode, 1);
	host_set_func(ctx, n, "audInfo", js_info, 1);
	host_set_func(ctx, n, "audFree", js_buf_free, 1);
	host_set_func(ctx, n, "audPlay", js_play, 6);
	host_set_func(ctx, n, "audStop", js_stop, 1);
	host_set_func(ctx, n, "audSet", js_set, 6);
	host_set_func(ctx, n, "audPos", js_pos, 1);
	host_set_func(ctx, n, "audEnded", js_ended, 0);
}
