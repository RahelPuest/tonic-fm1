/* SPDX-License-Identifier: MPL-2.0
 * TONIC: the app's half of the contract with fm1-base (base/device/unity.c, base/host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "tonic.h"

#define APP_NAME "TONIC"
#define APP_TAGLINE "FM GROOVEBOX"
#define APP_SPLASH_RGB RGB(236, 140, 72)
#define APP_CANVAS_PX (240u * 108u)       /* the drawing strips: 108 rows at most */
#define APP_BLOCK 256u                   /* frames per audio block (5.8 ms); the engine works in 32-sample steps */
#define APP_TOUCH                        /* velocity and pressure from the keys (lib/touch.h) */
#define APP_OVERLOAD                     /* the render past 90 % of its time: shed voices */
void app_overload(void);

/* the store: settings (with the project's header) in the settings sector pair, the panel calibration, the
 * project's sound pool, the user sound library, the 32 patterns. Two 4 KiB sectors each. */
#define OBJ_PAT0 5
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_POOL, OBJ_LIB0, OBJ_LIB1, OBJ_NOBJ = OBJ_PAT0 + NPAT };
#define S2(n) (0x97000u + 0x2000u * (n))
#define APP_STORE_MAP {0xFC000u, S2(0), S2(1), S2(2), S2(3), \
    S2(4), S2(5), S2(6), S2(7), S2(8), S2(9), S2(10), S2(11), S2(12), S2(13), S2(14), S2(15), S2(16), S2(17), \
    S2(18), S2(19), S2(20), S2(21), S2(22), S2(23), S2(24), S2(25), S2(26), S2(27), S2(28), S2(29), S2(30), \
    S2(31), S2(32), S2(33), S2(34), S2(35)}

/* backup and restore over USB (base/tools/fm1_backup.py): every object of the project */
#define APP_BACKUP_OBJS {OBJ_SETTINGS, OBJ_POOL, OBJ_LIB0, OBJ_LIB1, \
    5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, \
    33, 34, 35, 36}
#define APP_BACKUP_MAGIC 0x31544E54u     /* "TNT1" */
#define APP_BACKUP_VERSION 1
#define APP_BACKUP_WHAT "replaces the project"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* host simulator checks (base/host/plat_host.c "expect") */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD                     /* host commands: sound N (factory sound N on the active track) */
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
