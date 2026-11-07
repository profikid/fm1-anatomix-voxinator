/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the app's half of the contract with fm1-base (device/unity.c, host/plat_host.c)
 * and with its own settings. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "vox.h"
#include "ui.h"
#include "anatomy.h"
#include "app_state.h"

#define APP_NAME "ANATOMIX"
#define APP_TAGLINE "VOCAL PROCESSOR"
#define APP_SPLASH_RGB RGB(232, 96, 104)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_CANVAS_PX (240u * 176u)      /* the anatomy band: 176 rows at most (the pool's room) */

/* the store: the settings (with the current preset), the panel calibration (the platform's), Bluetooth's
 * address and pairing, and the 16 user presets. */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_PRESETS, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u, 0xDA000u}

/* the contract: the platform calls these */
void app_init(void);                   /* boot: the engine, then the saved settings (or the defaults) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                  /* silence now (before an update rewrites the flash) */
void app_ui_init(void);                /* the first screen, after the splash */
void app_frame(void);                  /* the UI: the input and a redraw, about 60 times a second */
void app_input_only(void);             /* between the frames: the input only */
void app_say(const char *a, const char *b);   /* a short message on the screen and to a screen reader */

/* the host simulator's checks: page, mode, note, level, preset, voice count.
 * Host-only: the device's libc has no strcmp, and nothing on the device needs these. */
#ifdef PLAT_HOST
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#endif
