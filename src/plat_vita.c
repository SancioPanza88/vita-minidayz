/* PS Vita platform: vitaGL, SceCtrl/SceTouch, SceAudio. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/audioout.h>
#include <psp2/power.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/apputil.h>
#include <psp2/system_param.h>
#include <psp2/sysmodule.h>
#include <psp2/message_dialog.h>
#include <psp2/common_dialog.h>
#include <vitaGL.h>
#include "platform.h"

/* QuickJS recurses deeply while parsing/executing; give it room. */
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;
/* newlib heap: JS heap + decoded assets. The rest of the RAM is left to vitaGL. */
int _newlib_heap_size_user = 230 * 1024 * 1024;

#define SAVE_DIR "ux0:data/MiniDayZ/"

static SceKernelLwMutexWork audio_mtx;
static AudioMixFn mixfn;
static FILE *logf_;
static SceUInt64 t0;

void plat_log(const char *s) {
	if (!logf_) logf_ = fopen(SAVE_DIR "log.txt", "w");
	if (logf_) { fputs(s, logf_); fputc('\n', logf_); fflush(logf_); }
}

static int file_exists(const char *p) {
	SceIoStat st;
	return sceIoGetstat(p, &st) >= 0;
}

static void fatal_dialog(const char *msg) {
	SceMsgDialogUserMessageParam mp;
	SceMsgDialogParam p;
	memset(&mp, 0, sizeof(mp));
	mp.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
	mp.msg = (const SceChar8 *)msg;
	sceMsgDialogParamInit(&p);
	p.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
	p.userMsgParam = &mp;
	if (sceMsgDialogInit(&p) < 0) return;
	while (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		vglSwapBuffers(GL_TRUE);
	}
	sceMsgDialogTerm();
}

int plat_init(void) {
	sceIoMkdir("ux0:data", 0777);
	sceIoMkdir("ux0:data/MiniDayZ", 0777);
	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);

	SceAppUtilInitParam ip; SceAppUtilBootParam bp;
	memset(&ip, 0, sizeof(ip)); memset(&bp, 0, sizeof(bp));
	sceAppUtilInit(&ip, &bp);

	vglInitExtended(0, SCREEN_W, SCREEN_H, 8 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
	vglUseVram(GL_TRUE);

	if (!file_exists("ur0:data/libshacccg.suprx") && !file_exists("ur0:data/external/libshacccg.suprx")) {
		fatal_dialog("libshacccg.suprx non trovato.\nlibshacccg.suprx not found.\n\n"
			"Installalo con ShaRKBR33D / Install it with ShaRKBR33D:\n"
			"github.com/Rinnegatamante/ShaRKBR33D");
		return -1;
	}
	if (!file_exists("app0:game/data.js")) {
		fatal_dialog("Dati del gioco mancanti (app0:game/data.js).\nGame data missing.");
		return -1;
	}

	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	sceKernelCreateLwMutex(&audio_mtx, "audio", 0, 0, NULL);
	t0 = sceKernelGetProcessTimeWide();
	return 0;
}

void plat_shutdown(void) {
	if (logf_) fclose(logf_);
	sceKernelExitProcess(0);
}

static float axis(unsigned char v) {
	float f = ((int)v - 128) / 127.0f;
	if (f > 1) f = 1;
	if (f < -1) f = -1;
	return f;
}

void plat_poll(InputState *in, int frame) {
	SceCtrlData pad;
	memset(&pad, 0, sizeof(pad));
	sceCtrlPeekBufferPositive(0, &pad, 1);
	static const unsigned int bits[BTN_COUNT] = {
		SCE_CTRL_CROSS, SCE_CTRL_CIRCLE, SCE_CTRL_SQUARE, SCE_CTRL_TRIANGLE,
		SCE_CTRL_LTRIGGER, SCE_CTRL_RTRIGGER, SCE_CTRL_START, SCE_CTRL_SELECT,
		SCE_CTRL_UP, SCE_CTRL_DOWN, SCE_CTRL_LEFT, SCE_CTRL_RIGHT };
	for (int i = 0; i < BTN_COUNT; i++) in->buttons[i] = (pad.buttons & bits[i]) ? 1 : 0;
	in->lx = axis(pad.lx); in->ly = axis(pad.ly);
	in->rx = axis(pad.rx); in->ry = axis(pad.ry);

	SceTouchData t;
	memset(&t, 0, sizeof(t));
	sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1);
	in->touch_count = t.reportNum > MAX_TOUCH ? MAX_TOUCH : t.reportNum;
	for (int i = 0; i < in->touch_count; i++) {
		in->touch_id[i] = t.report[i].id;
		in->touch_x[i] = t.report[i].x * SCREEN_W / 1920.0f;
		in->touch_y[i] = t.report[i].y * SCREEN_H / 1088.0f;
	}
	in->quit = 0;
}

void plat_swap(int frame) {
	vglSwapBuffers(GL_FALSE);
}

double plat_now_ms(void) {
	return (sceKernelGetProcessTimeWide() - t0) / 1000.0;
}

#define AUDIO_FRAMES 1024
static int audio_thread(SceSize args, void *argp) {
	int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, AUDIO_FRAMES, 48000, SCE_AUDIO_OUT_MODE_STEREO);
	if (port < 0) { plat_log("audio port open failed"); return 0; }
	int vol[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
	sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
	static int16_t buf[2][AUDIO_FRAMES * 2];
	int cur = 0;
	for (;;) {
		mixfn(buf[cur], AUDIO_FRAMES);
		sceAudioOutOutput(port, buf[cur]);
		cur ^= 1;
	}
	return 0;
}

void plat_audio_start(AudioMixFn fn) {
	mixfn = fn;
	SceUID th = sceKernelCreateThread("audio", audio_thread, 0x10000100 - 10, 64 * 1024, 0, 0, NULL);
	if (th >= 0) sceKernelStartThread(th, 0, NULL);
}

void plat_audio_lock(void) { sceKernelLockLwMutex(&audio_mtx, 1, NULL); }
void plat_audio_unlock(void) { sceKernelUnlockLwMutex(&audio_mtx, 1); }
const char *plat_game_dir(void) { return "app0:game/"; }
const char *plat_res_dir(void) { return "app0:res/"; }
const char *plat_save_dir(void) { return SAVE_DIR; }

const char *plat_lang(void) {
	int l = 1;
	sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &l);
	switch (l) {
	case SCE_SYSTEM_PARAM_LANG_ITALIAN: return "it-IT";
	case SCE_SYSTEM_PARAM_LANG_FRENCH: return "fr-FR";
	case SCE_SYSTEM_PARAM_LANG_SPANISH: return "es-ES";
	case SCE_SYSTEM_PARAM_LANG_GERMAN: return "de-DE";
	case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_PT: return "pt-PT";
	case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_BR: return "pt-BR";
	case SCE_SYSTEM_PARAM_LANG_RUSSIAN: return "ru-RU";
	default: return "en-US";
	}
}
