/* SPDX-License-Identifier: MPL-2.0
 * TONIC screens, drawn as the Digitone's OLED: a 1-bit 120 x 120 picture shown at 2x on the 240 x 240 LCD.
 *   rows 0..63    the Digitone's screen: the pattern in a box, the sound's name in an inverted bar; the page's box
 *                 and the track's level at the left; on the SYN pages the algorithm; the page's eight parameters,
 *                 two rows of four, each a small picture over its name (the row the knobs edit: names inverted)
 *   rows 64..119  what the Digitone shows on its keys and LEDs: the track's 16 steps, the page of steps, the mode,
 *                 the tracks, the tempo and transport, the held step
 * The home screen, the performance layer and the save page use the same picture. Only the bands of the screen
 * that changed are sent. Part of app.c. */
#include "oled_font.h"

#define OW 120                    /* the layout's units: 120 x 120, each 2 x 2 pixels of the 240 x 240 LCD */
#define OH 120
#define HW 240                    /* the picture itself: full resolution, 1 bit, and a colour for each 4 x 4 cell */
static uint8_t hb[HW][HW / 8] PLAT_POOL;
static uint8_t hc[HW / 4][HW / 8];   /* colour indices, two cells a byte (high nibble: the even cell) */
static uint32_t band_sum[10];
static uint8_t ob_valid;
static uint8_t drawn_step = 255;
static uint8_t o_ink = 1;         /* the colour that what is drawn now takes (K_*) */

/* the colours: indices into the theme's palette [TONIC] */
enum { K_BG, K_FG, K_DIM, K_ACC, K_T1, K_T2, K_T3, K_T4, K_MLO, K_MMID, K_MHI, NK };
#define C3(r, g, b) RGB(r, g, b)
static const uint16_t THEMES[NTHEME][NK] = {
    /* OLED: the Digitone's white, the tracks in four colours, a cyan accent */
    {C3(0, 0, 0), C3(226, 236, 255), C3(104, 112, 132), C3(80, 210, 255), C3(255, 104, 84), C3(255, 186, 56),
     C3(112, 222, 120), C3(124, 150, 255), C3(80, 220, 110), C3(250, 210, 60), C3(255, 70, 60)},
    /* AMBER: a warm monochrome */
    {C3(10, 5, 0), C3(255, 176, 40), C3(120, 76, 20), C3(255, 236, 170), C3(255, 120, 50), C3(255, 204, 80),
     C3(214, 226, 110), C3(255, 150, 120), C3(255, 176, 40), C3(255, 220, 120), C3(255, 90, 40)},
    /* PHOSPHOR: a green CRT */
    {C3(0, 10, 2), C3(110, 255, 130), C3(40, 110, 54), C3(210, 255, 210), C3(70, 255, 150), C3(190, 255, 90),
     C3(80, 230, 230), C3(230, 255, 170), C3(110, 255, 130), C3(220, 255, 120), C3(255, 120, 80)},
    /* BLUE: white on night blue, an orange accent */
    {C3(8, 14, 38), C3(226, 232, 255), C3(96, 110, 160), C3(255, 160, 60), C3(255, 110, 110), C3(255, 200, 80),
     C3(120, 230, 160), C3(130, 190, 255), C3(110, 230, 140), C3(255, 210, 80), C3(255, 80, 70)},
    /* PAPER: dark on light */
    {C3(236, 238, 242), C3(24, 28, 44), C3(150, 154, 168), C3(0, 112, 220), C3(210, 50, 40), C3(196, 120, 0),
     C3(20, 140, 60), C3(40, 80, 210), C3(20, 150, 70), C3(200, 140, 0), C3(210, 40, 30)},
};
static const uint8_t K_TRK[4] = {K_T1, K_T2, K_T3, K_T4};

/* ------------------------------------------------------------------ the picture --- */
/* a pixel of the full picture; an "on" pixel gives its cell the ink */
static inline void h_px(int x, int y, int on)
{
    if ((unsigned)x >= HW || (unsigned)y >= HW)
        return;
    if (on) {
        uint8_t *c = &hc[y >> 2][x >> 3];
        hb[y][x >> 3] |= (uint8_t)(0x80u >> (x & 7));
        *c = (x & 4) ? (uint8_t)((*c & 0xF0) | o_ink) : (uint8_t)((*c & 0x0F) | o_ink << 4);
    } else
        hb[y][x >> 3] &= (uint8_t)~(0x80u >> (x & 7));
}
static void h_rect(int x, int y, int w, int h, int on)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            h_px(i, j, on);
}
static void h_line(int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int e = dx + dy;
    for (;;) {
        int e2 = 2 * e;
        h_px(x0, y0, 1);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
/* a line in layout units with fractions (curves): full-resolution pixels */
static void o_fline(float x0, float y0, float x1, float y1)
{
    h_line((int)(x0 * 2.0f + 0.5f), (int)(y0 * 2.0f + 0.5f), (int)(x1 * 2.0f + 0.5f), (int)(y1 * 2.0f + 0.5f));
}
/* a dot (a moving point on a picture): a small round blob */
static void o_dot(float x, float y)
{
    int cx = (int)(x * 2.0f + 0.5f), cy = (int)(y * 2.0f + 0.5f);
    h_rect(cx - 1, cy - 2, 3, 5, 1);
    h_rect(cx - 2, cy - 1, 5, 3, 1);
}

/* the layout's primitives (units of 2 x 2 pixels): fills are whole units, lines and frames one pixel thin */
static void o_px(int x, int y, int on) { h_rect(2 * x, 2 * y, 2, 2, on); }
static void o_rect(int x, int y, int w, int h, int on) { h_rect(2 * x, 2 * y, 2 * w, 2 * h, on); }
static void o_hline(int x, int y, int w, int dotted)
{
    int i;
    for (i = 0; i < w; i++)
        if (!dotted || !(i & 1)) {
            h_px(2 * (x + i), 2 * y, 1);
            if (!dotted) h_px(2 * (x + i) + 1, 2 * y, 1);
        }
}
static void o_vline(int x, int y, int h, int dotted)
{
    int i;
    for (i = 0; i < h; i++)
        if (!dotted || !(i & 1)) {
            h_px(2 * x, 2 * (y + i), 1);
            if (!dotted) h_px(2 * x, 2 * (y + i) + 1, 1);
        }
}
static void o_frame(int x, int y, int w, int h)      /* one pixel thin, the corners rounded */
{
    int X = 2 * x, Y = 2 * y, W = 2 * w, H = 2 * h, i;
    for (i = 2; i < W - 2; i++) { h_px(X + i, Y, 1); h_px(X + i, Y + H - 1, 1); }
    for (i = 2; i < H - 2; i++) { h_px(X, Y + i, 1); h_px(X + W - 1, Y + i, 1); }
    h_px(X + 1, Y + 1, 1); h_px(X + W - 2, Y + 1, 1); h_px(X + 1, Y + H - 2, 1); h_px(X + W - 2, Y + H - 2, 1);
}
static void o_box(int x, int y, int w, int h)        /* filled, the corners rounded */
{
    int X = 2 * x, Y = 2 * y, W = 2 * w, H = 2 * h;
    h_rect(X + 2, Y, W - 4, H, 1);
    h_rect(X, Y + 2, W, H - 4, 1);
    h_rect(X + 1, Y + 1, 1, H - 2, 1);
    h_rect(X + W - 2, Y + 1, 1, H - 2, 1);
}
static void o_line(int x0, int y0, int x1, int y1) { h_line(2 * x0, 2 * y0, 2 * x1, 2 * y1); }
static int o_glyph_i(int ch)
{
    if (ch >= 'a' && ch <= 'z')
        ch -= 32;
    return ch < 32 || ch > 95 ? '?' - 32 : ch - 32;
}
/* S: 4 units a character (the glyph 5 x 9 pixels, thin); returns the end x */
static int o_ts(int x, int y, const char *s, int on)
{
    for (; *s; s++, x += 4) {
        const uint8_t *g = OF_S9[o_glyph_i((uint8_t)*s)];
        int r, c;
        for (r = 0; r < 9; r++)
            for (c = 0; c < 5; c++)
                if (g[r] >> (4 - c) & 1u)
                    h_px(2 * x + c, 2 * y + r, on);
    }
    return x;
}
/* M: 6 units a character (the glyph 10 x 14 pixels, smoothed); k = 2: 12 units (20 x 28) */
static int o_tm(int x, int y, const char *s, int on, int k)
{
    for (; *s; s++, x += 6 * k) {
        int gi = o_glyph_i((uint8_t)*s), r, c;
        if (k >= 2) {
            const uint32_t *g = OF_M28[gi];
            for (r = 0; r < 28; r++)
                for (c = 0; c < 20; c++)
                    if (g[r] >> (19 - c) & 1u)
                        h_px(2 * x + c, 2 * y + r, on);
        } else {
            const uint16_t *g = OF_M14[gi];
            for (r = 0; r < 14; r++)
                for (c = 0; c < 10; c++)
                    if (g[r] >> (9 - c) & 1u)
                        h_px(2 * x + c, 2 * y + r, on);
        }
    }
    return x;
}
static int o_ws(const char *s) { int n = tx_len(s); return n ? n * 4 - 1 : 0; }
static int o_wm(const char *s, int k) { int n = tx_len(s); return n ? (n * 6 - 1) * k : 0; }
static void o_ts_c(int x, int w, int y, const char *s, int on) { o_ts(x + (w - o_ws(s) + 1) / 2, y, s, on); }
static void o_clear(void)
{
    memset(hb, 0, sizeof hb);
    memset(hc, 0x11, sizeof hc);
    o_ink = K_FG;
}

/* the picture to the LCD in ten bands of 24 rows: a band goes only when its pixels or colours changed */
static void o_send(void)
{
    const uint16_t *pal = THEMES[set.theme < NTHEME ? set.theme : 0];
    uint16_t sp[NK];
    int b, k;
    for (k = 0; k < NK; k++)
        sp[k] = swap16(pal[k]);
    for (b = 0; b < 10; b++) {
        uint32_t h = 2166136261u ^ set.theme;
        int y, x;
        const uint8_t *p = hb[b * 24], *q = hc[b * 6];
        for (k = 0; k < 24 * (HW / 8); k++)
            h = (h ^ p[k]) * 16777619u;
        for (k = 0; k < 6 * (HW / 8); k++)
            h = (h ^ q[k]) * 16777619u;
        if (ob_valid && h == band_sum[b])
            continue;
        band_sum[b] = h;
        cv_begin(240, 24, C_BLACK);
        for (y = 0; y < 24; y++) {
            const uint8_t *row = hb[b * 24 + y], *cr = hc[(b * 24 + y) >> 2];
            uint16_t *o = cv_px + (uint32_t)y * 240u;
            for (x = 0; x < HW; x += 8) {
                uint8_t bits = row[x >> 3], cc = cr[x >> 3];
                uint16_t c0 = sp[cc >> 4], c1 = sp[cc & 15], bg = sp[K_BG];
                o[x] = bits & 0x80 ? c0 : bg; o[x + 1] = bits & 0x40 ? c0 : bg;
                o[x + 2] = bits & 0x20 ? c0 : bg; o[x + 3] = bits & 0x10 ? c0 : bg;
                o[x + 4] = bits & 0x08 ? c1 : bg; o[x + 5] = bits & 0x04 ? c1 : bg;
                o[x + 6] = bits & 0x02 ? c1 : bg; o[x + 7] = bits & 0x01 ? c1 : bg;
            }
        }
        cv_blit(0, (uint32_t)b * 24u);
    }
    ob_valid = 1;
}

static void pat_name(int idx, char *b)
{
    b[0] = (char)('A' + idx / 16);
    b[1] = (char)('0' + (idx % 16 + 1) / 10);
    b[2] = (char)('0' + (idx % 16 + 1) % 10);
    b[3] = 0;
}

/* ------------------------------------------------------------- the Digitone part --- */
/* the header: [A01] and an inverted bar with the name (and the page's place, 1/2) */
static void o_header(const char *name, const char *right)
{
    char t[8];
    int x;
    pat_name(cur_idx, t);
    if (next_idx >= 0 && (plat_ms() / 250u) & 1u)    /* the cued pattern blinks in the box */
        pat_name(next_idx, t);
    o_frame(0, 0, 21, 9);
    o_tm(2, 1, t, 1, 1);
    o_ink = K_TRK[ui.trk];                           /* the name bar in the track's colour */
    o_box(23, 0, 97, 9);
    x = right && right[0] ? 118 - o_wm(right, 1) : 118;
    if (right && right[0] && 25 + 6 * tx_len(name) > x - 3)   /* a long name: the counter small [0.9.1] */
        x = 119 - o_ws(right);
    {
        char n[24];
        int k = 0;
        while (name[k] && k < 23 && 25 + (k + 1) * 6 - 1 <= x - 3) {
            n[k] = name[k];
            k++;
        }
        n[k] = 0;
        o_tm(25, 1, n, 0, 1);
    }
    if (right && right[0]) {
        if (x > 118 - o_wm(right, 1))
            o_ts(x, 2, right, 0);
        else
            o_tm(x, 1, right, 0, 1);
    }
    o_ink = K_FG;
}

/* the page tabs under the header: the group's pages, the one shown inverted in the accent colour; too many for
 * the width (GLO): a window around it, the ends marked [TONIC] */
static void o_tabs(const row_t *cur)
{
    const char *tt[32];
    const row_t *first[32];
    int n = 0, i, me = 0, w[32], tot = 0, a, b, x;
    const char *last = 0;
    for (i = 0; i < grp_n(ui.grp) && n < 32; i++)
        if (grp_rows(ui.grp)[i].title != last) {
            last = grp_rows(ui.grp)[i].title;
            first[n] = &grp_rows(ui.grp)[i];
            tt[n] = row_title(first[n]);
            if (cur->title == last)
                me = n;
            w[n] = o_ws(tt[n]) + 4;
            n++;
        }
    if (n < 2)
        return;
    for (i = 0; i < n; i++)
        tot += w[i] + 2;
    a = 0;
    b = n;
    if (tot > OW) {                                  /* a window around the page shown */
        int sum;
        a = b = me;
        sum = w[me] + 2;
        while (1) {
            int grew = 0;
            if (b + 1 <= n - 1 && sum + w[b + 1] + 2 <= OW - 10) { b++; sum += w[b] + 2; grew = 1; }
            if (a - 1 >= 0 && sum + w[a - 1] + 2 <= OW - 10) { a--; sum += w[a] + 2; grew = 1; }
            if (!grew) break;
        }
        b++;
    }
    x = a > 0 ? 5 : 0;
    o_ink = K_DIM;
    if (a > 0) o_ts(0, 11, "<", 1);
    if (b < n) o_ts(OW - 4, 11, ">", 1);
    for (i = a; i < b; i++) {
        if (i == me) {
            o_ink = K_ACC;
            o_box(x, 10, w[i], 7);
            o_ts(x + 2, 11, tt[i], 0);
        } else {
            o_ink = K_DIM;
            o_ts(x + 2, 11, tt[i], 1);
        }
        x += w[i] + 2;
    }
    o_ink = K_FG;
}

/* the meters at the left: the four tracks in their colours, the master (green, yellow, red), labels under them;
 * the active track's label inverted [TONIC] */
static int meter_h(float lvl, int h)
{
    float db = lvl > 1e-5f ? 6.0206f * fm_log2f(lvl) : -100.0f;   /* -42 dB .. 0 dB */
    return clamp((int)((db + 42.0f) * (float)h / 42.0f), 0, h);
}
static void o_meters(int y0, int h)
{
    int t, mh;
    for (t = 0; t < NTRK; t++) {
        int x = 1 + t * 3, lh = meter_h(engine_level(t) * 0.25f, h);
        o_ink = K_TRK[t];
        if (eng.mute >> t & 1u)
            o_hline(x, y0 + h - 1, 2, 1);
        else {
            o_rect(x, y0 + h - lh, 2, lh, 1);
            if (lh < h) o_px(x, y0, 1);              /* a mark at the top of the scale */
        }
        {
            char d[2] = {(char)('1' + t), 0};
            if (t == ui.trk) { o_rect(x - 1, y0 + h + 1, 4, 6, 1); o_ts(x, y0 + h + 2, d, 0); }
            else o_ts(x, y0 + h + 2, d, 1);
        }
    }
    mh = meter_h(engine_master_level(), h);         /* the master, by segment */
    for (t = 0; t < mh; t++) {
        o_ink = t >= h * 9 / 10 ? K_MHI : t >= h * 7 / 10 ? K_MMID : K_MLO;
        o_rect(13, y0 + h - 1 - t, 3, 1, 1);
    }
    o_ink = K_DIM;
    o_vline(12, y0, h, 1);
    o_ts(13, y0 + h + 2, "M", 1);
    o_ink = K_FG;
}

/* the page's box (SYN 1 ...) and the track's level */
static void o_pagebox(const char *title)
{
    char w[5];
    int k = 0, d = -1, lvl = pcur->trk[ui.trk].tp[P_TLVL - NSP], h;
    while (title[k] && title[k] != ' ' && k < 4) {
        if (title[k] >= '0' && title[k] <= '9')
            d = title[k] - '0';
        k++;
    }
    {
        int n = k > 4 ? 4 : k, j, m = 0;
        for (j = 0; j < n; j++)
            if (title[j] < '0' || title[j] > '9')
                w[m++] = title[j];
        w[m > 4 ? 4 : m] = 0;
    }
    o_frame(0, 11, 16, 22);
    o_ts_c(0, 16, 13, w, 1);
    if (d >= 0) {
        char ds[2] = {(char)('0' + d), 0};
        o_tm(5, 21, ds, 1, 1);
    } else {
        int i, n = grp_n(ui.grp);
        for (i = 0; i < n && i < 6; i++)               /* the rows of the group: dots */
            o_rect(2 + i * 2, 24, 1, i == ui.row[ui.grp] ? 3 : 1, 1);
    }
    /* LEV: the track's level, dotted guides as the Digitone draws them */
    o_vline(1, 36, 19, 1);
    o_vline(14, 36, 19, 1);
    o_frame(4, 36, 8, 19);
    h = lvl * 15 / 127;
    o_rect(6, 53 - h, 4, h, 1);
    o_ts(2, 58, "LEV", 1);
}

/* the algorithm: operators as boxes (C A B B, the HARM-shaped ones filled), the modulation lines, the feedback
 * loop at the corner of its operator, and the dotted X / Y outputs */
static void o_algo(int x0, int y0, int algo, int harm)
{
    static const int8_t POS[8][4][2] = {
        {{0, 2}, {0, 1}, {1, 1}, {1, 0}}, {{0, 1}, {0, 0}, {1, 1}, {1, 0}}, {{0, 1}, {1, 0}, {2, 1}, {1, 1}},
        {{0, 3}, {0, 2}, {0, 1}, {0, 0}}, {{0, 2}, {0, 1}, {0, 0}, {1, 0}}, {{0, 1}, {0, 0}, {1, 1}, {1, 0}},
        {{0, 1}, {0, 0}, {1, 1}, {1, 0}}, {{0, 1}, {0, 0}, {2, 1}, {1, 1}}};
    static const int8_t MODL[8][4][2] = {
        {{1, 0}, {3, 2}, {2, 0}, {-1, -1}}, {{1, 0}, {3, 2}, {-1, -1}, {-1, -1}}, {{1, 0}, {1, 3}, {1, 2}, {-1, -1}},
        {{3, 2}, {2, 1}, {1, 0}, {-1, -1}}, {{3, 2}, {2, 1}, {3, 1}, {1, 0}}, {{1, 0}, {1, 2}, {3, 0}, {3, 2}},
        {{1, 0}, {3, 2}, {-1, -1}, {-1, -1}}, {{1, 0}, {-1, -1}, {-1, -1}, {-1, -1}}};
    static const int8_t FBOP[8] = {1, 3, 1, 3, 2, 1, 1, 2};
    /* the operators on the outputs: bit 0 X, bit 1 Y */
    static const uint8_t OUT[8][4] = {{1, 0, 2, 0}, {1, 0, 2, 0}, {1, 0, 2, 1}, {1, 0, 2, 0},
                                      {1, 2, 0, 0}, {1, 0, 2, 0}, {1, 1, 2, 2}, {1, 0, 2, 1}};
    static const char *const OPN[4] = {"C", "A", "B", "B"};
    int k, rows = 0, sp = 11;
    for (k = 0; k < 4; k++)
        if (POS[algo][k][1] + 1 > rows)
            rows = POS[algo][k][1] + 1;
    if (rows == 4)
        sp = 10;
    for (k = 0; k < 4; k++) {                        /* the outputs: dotted lines down to X and Y */
        int x = x0 + POS[algo][k][0] * 9 + 3, y = y0 + POS[algo][k][1] * sp + 7;
        if (OUT[algo][k])
            o_vline(x, y, 52 - y, 1);
    }
    {   /* the modulation lines, as thick as the modulation now: LEV A x envelope A from A, LEV B x envelope B from
         * B1 / B2 (the track's newest voice; its set LEV when none sounds) [TONIC] */
        float ea, eb, da, db;
        const sound_t *sn = tsnd();
        if (!engine_env_view(ui.trk, 0, &ea)) ea = 1.0f;
        if (!engine_env_view(ui.trk, 1, &eb)) eb = 1.0f;
        da = (float)sn->p[P_ALEV] / 127.0f * ea;
        db = (float)sn->p[P_BLEV] / 127.0f * eb;
        for (k = 0; k < 4; k++) {
            int a = MODL[algo][k][0], b = MODL[algo][k][1], X0, Y0, X1, Y1, t, j;
            static const int8_t OFF[5][2] = {{0, 0}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}};
            if (a < 0)
                continue;
            X0 = 2 * (x0 + POS[algo][a][0] * 9 + 3); Y0 = 2 * (y0 + POS[algo][a][1] * sp + 3);
            X1 = 2 * (x0 + POS[algo][b][0] * 9 + 3); Y1 = 2 * (y0 + POS[algo][b][1] * sp + 3);
            t = 1 + (int)((a == 1 ? da : db) * 4.0f + 0.5f);
            o_ink = (a == 1 ? da : db) > 0.02f ? K_ACC : K_DIM;
            for (j = 0; j < t && j < 5; j++)
                h_line(X0 + OFF[j][0], Y0 + OFF[j][1], X1 + OFF[j][0], Y1 + OFF[j][1]);
        }
        o_ink = K_FG;
    }
    for (k = 0; k < 4; k++) {
        int x = x0 + POS[algo][k][0] * 9, y = y0 + POS[algo][k][1] * sp;
        int shaped = (harm < 0 && k == 0) || (harm > 0 && (k == 1 || k == 2));
        o_rect(x, y, 7, 7, 0);
        if (shaped) {
            o_box(x, y, 7, 7);
            o_ts(x + 2, y + 1, OPN[k], 0);
        } else {
            o_frame(x, y, 7, 7);
            o_ts(x + 2, y + 1, OPN[k], 1);
        }
        if (k == FBOP[algo]) {                       /* the feedback loop, at its upper left */
            o_px(x - 1, y - 1, 1); o_px(x, y - 2, 1); o_px(x + 1, y - 2, 1); o_px(x - 2, y, 1); o_px(x - 2, y + 1, 1);
        }
    }
    {
        int xs = -1, ys = -1;
        for (k = 0; k < 4; k++) {
            int x = x0 + POS[algo][k][0] * 9 + 2;
            if ((OUT[algo][k] & 1u) && xs < 0) xs = x;
            if ((OUT[algo][k] & 2u) && ys < 0) ys = x;
        }
        if (xs >= 0) o_ts(xs, 56, "X", 1);
        if (ys >= 0) o_ts(ys == xs ? ys + 4 : ys, 56, "Y", 1);
    }
}

/* the VA engine in place of the algorithm [TONIC]: the two oscillators' waves (SYNC links them), the sub and the
 * noise as bars, the engine's name */
static void o_vawave(int x, int y, int w, int h, int wave, int pw)
{
    int i, n = 2 * w;
    float px = 0, py = 0;
    for (i = 0; i <= n; i++) {                       /* two cycles, smooth; the edges straight */
        float t = (float)(i % (n / 2)) / (float)(n / 2), u, nx, ny;
        switch (wave) {
        case VW_SAW: u = t; break;
        case VW_SQR: u = t < 0.5f - 0.45f * (float)pw / 480.0f ? 1.0f : 0.0f; break;
        case VW_TRI: u = t < 0.5f ? 2.0f * t : 2.0f - 2.0f * t; break;
        case VW_SIN: u = 0.5f + 0.5f * fm_sin_turns(t); break;
        default: u = 0.0f; break;
        }
        nx = (float)x + (float)i * (float)(w - 1) / (float)n;
        ny = (float)(y + h) - 1.0f - u * (float)(h - 1);
        if (i) o_fline(px, py, nx, ny);
        px = nx;
        py = ny;
    }
}
static void o_va(int x0, int y0, const sound_t *s)
{
    int sub = s->p[V_SUB], ns = s->p[V_NOIS];
    o_ts(x0, y0 + 2, "1", 1);
    o_vawave(x0 + 5, y0, 20, 8, s->p[V_WAV1], s->p[V_PW]);
    o_ts(x0, y0 + 13, "2", 1);
    o_vawave(x0 + 5, y0 + 11, 20, 8, s->p[V_WAV2], s->p[V_PW]);
    if (s->p[V_SYNC])
        o_vline(x0 + 3, y0 + 7, 5, 1);
    o_ts(x0, y0 + 24, "S", 1);
    o_frame(x0 + 5, y0 + 24, 20, 5);
    o_rect(x0 + 6, y0 + 25, sub * 18 / 127, 3, 1);
    o_ts(x0, y0 + 32, "N", 1);
    o_frame(x0 + 5, y0 + 32, 20, 5);
    o_rect(x0 + 6, y0 + 33, ns * 18 / 127, 3, 1);
    o_tm(x0 + 3, y0 + 41, "VA", 1, 1);
}

/* a DRUM kit [TONIC]: its 8 pads, 2 x 4, each with its model's letters; lit while it sounds, the pad the grid
 * places (the track's NOTE) in the foreground */
static void o_kit(int x0, int y0, const sound_t *s)
{
    int i, cp = pad_of(ttrk()->tp[P_NOTE - NSP]);
    for (i = 0; i < NPAD; i++) {
        int x = x0 + (i & 1) * 13, y = y0 + (i >> 1) * 10;
        const char *nm = DM_SHORT[s->s[S_DM1 + i] % NDM];
        if (engine_pad_level(ui.trk, i) > 0.03f) {
            o_ink = K_ACC;
            o_box(x, y, 12, 9);
            o_ts_c(x, 12, y + 2, nm, 0);
        } else {
            o_ink = i == cp ? K_FG : K_DIM;
            o_frame(x, y, 12, 9);
            o_ts_c(x, 12, y + 2, nm, 1);
        }
    }
    o_ink = K_FG;
    o_tm(x0 + 1, y0 + 41, "KIT", 1, 1);
}

/* small pictures for a slot (x, y: the slot's top left; w the slot's width; 18 rows) */
/* an envelope, smooth (the decays and the release as the engine's exponential curves); which (0 operator A,
 * 1 operator B, 2 filter, 3 amp): a dot where the track's newest voice is on it now [TONIC] */
static void o_curve(float x0, float y0, float x1, float y1, int expo)
{
    int i;
    float px = x0, py = y0;
    for (i = 1; i <= 8; i++) {
        float u = (float)i / 8.0f, k = expo ? (1.0f - fm_exp2f(-5.0f * u)) / (1.0f - fm_exp2f(-5.0f)) : u;
        float nx = x0 + (x1 - x0) * u, ny = y0 + (y1 - y0) * k;
        o_fline(px, py, nx, ny);
        px = nx;
        py = ny;
    }
}
static void o_env(int x, int y, int w, int h, int a, int d, int s, int r, int adsr, int which)
{
    float W = (float)(w - 2), x0 = (float)x + 1.0f, yb = (float)(y + h) - 1.5f, yt = (float)y + 1.5f;
    float xa = x0 + (float)a * W / 4.0f / 127.0f, xd = xa + (float)d * W / 4.0f / 127.0f, xs = xd + W / 5.0f;
    float xr = xs + (r >= 127 ? W / 4.0f : (float)r * W / 4.0f / 127.0f), sl = (float)s / 127.0f;
    float ys = yb - sl * (yb - yt), xe = (float)(x + w) - 1.5f, lvl;
    int st;
    if (xr > xe) xr = xe;
    o_frame(x, y, w, h);
    o_curve(x0, yb, xa, yt, 0);
    o_curve(xa, yt, xd, ys, 1);
    if (adsr) {
        o_fline(xd, ys, xs, ys);
        if (r >= 127) o_fline(xs, ys, xr, ys);
        else o_curve(xs, ys, xr, yb, 1);
    } else
        o_fline(xd, ys, xe, ys);
    st = which >= 0 ? engine_env_view(ui.trk, which, &lvl) : 0;
    if (st && st < 6) {                              /* where the sound is now */
        float px, py = yb - lvl * (yb - yt);
        o_ink = K_ACC;
        switch (st) {
        case 1: px = x0; break;
        case 2: px = x0 + (xa - x0) * lvl; break;
        case 3: px = xa + (xd - xa) * fm_clampf((1.0f - lvl) / fm_maxf(1.0f - sl, 0.02f), 0, 1); break;
        case 4: px = adsr ? xd + (xs - xd) * (float)(plat_ms() % 1000u) / 1000.0f : xa; break;
        default: px = adsr ? xs + (xr - xs) * fm_clampf(1.0f - lvl / fm_maxf(sl, 0.02f), 0, 1)
                           : xd + (xe - xd) * 0.5f; break;
        }
        o_dot(px, py);
        o_ink = K_FG;
    }
}
/* an LFO wave, smooth; ph >= 0: a dot at that phase */
static float lfo_shape(int wave, float ph, int i)
{
    switch (wave) {
    case 0: return ph < 0.25f ? 4 * ph : ph < 0.75f ? 2 - 4 * ph : 4 * ph - 4;
    case 1: return fm_sin_turns(ph);
    case 2: return ph < 0.5f ? 1 : -1;
    case 3: return 1 - 2 * ph;
    case 4: return fm_exp2f(-12 * ph) * 2 - 1;
    case 5: return ph < 0.5f ? 4 * ph - 1 : -1;
    default: return (float)((i * 7919 / 5) % 7) / 3.0f - 1.0f;
    }
}
static void o_wave(int x, int y, int w, int h, int wave, float ph)
{
    int i, n = 2 * w;
    float px = 0, py = 0, hh = (float)h * 0.5f - 0.5f, yc = (float)y + (float)h * 0.5f;
    for (i = 0; i <= n; i++) {
        float u = (float)i / (float)n, nx = (float)x + u * (float)(w - 1), ny = yc - lfo_shape(wave, u, i / 2) * hh;
        if (i) o_fline(px, py, nx, ny);
        px = nx;
        py = ny;
    }
    if (ph >= 0.0f) {
        o_ink = K_ACC;
        o_dot((float)x + ph * (float)(w - 1), yc - lfo_shape(wave, ph, (int)(ph * (float)w)) * hh);
        o_ink = K_FG;
    }
}

/* the filter's response (FLTR row 2): the magnitude over 20 Hz .. 20 kHz, the cutoff now (with its envelope, from
 * the track's newest voice) solid, the set FREQ dotted when they differ; LP2, HP2 and LP4 as the engine's [TONIC] */
static float filt_db(int type, float f, float fc, float rs)
{
    float r = f / fc, re, im, m;
    if (type == 3) {                                 /* 4 one-poles with feedback k, input x (1 + k) */
        float k = 3.5f * (rs / 128.0f) * (rs / 128.0f), a = 1.0f, b = 0.0f, j;
        for (j = 0; j < 4; j++) { float na = a - b * r, nb = b + a * r; a = na; b = nb; }   /* (1 + j r)^4 */
        re = a + k; im = b;
        m = (1.0f + k) / fm_sqrtf(re * re + im * im);
    } else {
        float q = reso_q(rs), d = 1.0f - r * r, l = 1.0f / fm_sqrtf(d * d + (r / q) * (r / q));
        m = type == 2 ? l * r * r : l;
    }
    return 6.0206f * fm_log2f(fm_maxf(m, 1e-4f));
}
static void o_filter(int x, int y, int w, int h, int type, float fc_now, float fc_set, float rs)
{
    int pass;
    o_frame(x, y, w, h);
    if (!type) {
        o_ink = K_DIM;
        o_ts_c(x, w, y + h / 2 - 2, "FILTER OFF", 1);
        o_ink = K_FG;
        return;
    }
    for (pass = 0; pass < 2; pass++) {
        float fc = pass ? fc_now : fc_set, hz = cutoff_hz(fc), px = 0, py = 0, cx;
        int i, n = 2 * (w - 2);
        if (pass && fm_fabsf(fc_now - fc_set) < 0.5f)
            break;
        o_ink = pass ? K_ACC : (fm_fabsf(fc_now - fc_set) < 0.5f ? K_FG : K_DIM);
        for (i = 0; i <= n; i++) {
            float u = (float)i / (float)n, f = 20.0f * fm_exp2f(u * 9.966f), db = filt_db(type, f, fm_minf(hz, 20000.0f), rs);
            float nx = (float)x + 1.0f + u * (float)(w - 3);
            float ny = (float)y + 3.0f + fm_clampf(-db + 12.0f, 0.0f, 48.0f) * (float)(h - 5) / 48.0f;   /* +12 .. -36 dB */
            if (i && (!pass || (i & 1) == 0 || 1)) o_fline(px, py, nx, ny);
            px = nx;
            py = ny;
        }
        cx = (float)x + 1.0f + fm_clampf(fm_log2f(fm_maxf(hz, 20.0f) / 20.0f) / 9.966f, 0, 1) * (float)(w - 3);
        o_fline(cx, (float)(y + h) - 2.0f, cx, (float)(y + h) - 4.0f);
    }
    o_ink = K_FG;
}

/* a value too wide for its slot loses its last characters (after the point first) */
static void o_fit(char *v, int w)
{
    while (o_ws(v) > w && v[0])
        v[tx_len(v) - 1] = 0;
    if (v[0] && v[tx_len(v) - 1] == '.')
        v[tx_len(v) - 1] = 0;
}

static void o_slot(const item_t *it, int x, int y, int w, int active)
{
    range_t rg;
    char v[24];
    int val, lk, id = it->id, isnd = it->kind == IK_SND || it->kind == IK_TRK;
    if (isnd && param_desc(tsnd()->s[S_ENGINE], id)->lo == param_desc(tsnd()->s[S_ENGINE], id)->hi && it->kind == IK_SND)
        it = &(const item_t){IK_NONE, 0};            /* a parameter this engine does not use: an empty slot [0.9.1] */
    if (it->kind == IK_NONE) {                       /* an empty slot: a dotted box, as on the Digitone */
        int i;
        for (i = 0; i < w - 2; i += 2) { o_px(x + i, y + 3, 1); o_px(x + i, y + 16, 1); }
        for (i = 0; i < 13; i += 2) { o_px(x, y + 3 + i, 1); o_px(x + w - 3, y + 3 + i, 1); }
        return;
    }
    rg = item_range(it);
    val = item_get(it, &lk);
    /* the name below, inverted on the row the knobs edit */
    {
        char n[12];
        int k;
        for (k = 0; k < 11 && item_name(it)[k]; k++)
            n[k] = item_name(it)[k];
        n[k] = 0;
        o_fit(n, w - 1);
        if (active) {
            o_ink = K_ACC;
            o_rect(x - 1, y + 19, w, 7, 1);
            o_ts_c(x - 1, w, y + 20, n, 0);
        } else {
            o_ink = K_DIM;
            o_ts_c(x - 1, w, y + 20, n, 1);
        }
        o_ink = lk ? K_ACC : K_FG;                   /* a locked value in the accent colour */
    }
    if (isnd && id == P_ALGO && tsnd()->s[S_ENGINE] == ENG_FM) {   /* ALGO: the big number */
        char d[2] = {(char)('1' + val), 0};
        if (lk) { o_box(x + (w - 16) / 2, y + 1, 16, 17); o_tm(x + (w - 9) / 2, y + 3, d, 0, 2); }
        else { o_frame(x + (w - 16) / 2, y + 1, 16, 17); o_tm(x + (w - 9) / 2, y + 3, d, 1, 2); }
        return;
    }
    if (isnd && (id == P_PAN || (id == P_MIX && !drum_trk()))) {   /* a crossfader: X..Y, L..R */
        int span = rg.hi - rg.lo, p = x + 4 + (val - rg.lo) * (w - 11) / (span ? span : 1);
        int va = tsnd()->s[S_ENGINE] == ENG_VA;
        o_ts(x, y + 1, id == P_MIX ? (va ? "1" : "X") : "L", 1);
        o_ts(x + w - 5, y + 1, id == P_MIX ? (va ? "2" : "Y") : "R", 1);
        o_hline(x + 1, y + 11, w - 3, 0);
        o_rect(p - 1, y + 8, 3, 7, 1);
        if (lk) o_rect(x, y + 16, w - 2, 1, 1);
        return;
    }
    if (isnd && (id == P_L1WAV || id == P_L2WAV)) {  /* the LFO's wave */
        o_frame(x, y + 2, w - 2, 14);
        o_wave(x + 2, y + 4, w - 6, 10, val, engine_lfo_phase(ui.trk, id == P_L1WAV ? 0 : 1));
        return;
    }
    /* the value in a box; under it, where it is in its range */
    item_fmt(it, val, v);
    if (it->kind == IK_SET && id >= S_DM1 && id <= S_DM8)   /* a kit's model: its flavour (the name has the rest) */
        tx_cpy(v, DM_VAR[val % NDM]);
    o_fit(v, w - 4);
    if (lk) {
        o_box(x, y + 2, w - 2, 11);
        o_ts_c(x, w - 2, y + 5, v, 0);
    } else {
        o_frame(x, y + 2, w - 2, 11);
        o_ts_c(x, w - 2, y + 5, v, 1);
    }
    if (rg.hi - rg.lo >= 8 && rg.fmt != F_LIST) {
        int span = rg.hi - rg.lo, f = (val - rg.lo) * (w - 2) / span, i;
        for (i = 0; i < w - 2; i++)
            if (i <= f || !(i & 1))
                o_px(x + i, y + 15, 1);
        if (rg.lo < 0) {                             /* bipolar: the centre */
            int c = (-rg.lo) * (w - 2) / span;
            o_px(x + c, y + 16, 1);
        }
    }
}

/* a row of four: the envelope rows draw their shape across the slots unless a knob of the row just moved */
static void o_row(const row_t *rr, int x0, int pitch, int y, int active)
{
    const sound_t *s = tsnd();
    int k, graph = -1, touched = active && plat_ms() - ui.touch_t < 1500u;
    const item_t *it = rr->it;
    if (it[0].kind == IK_SND && !touched) {
        if (it[0].id == P_AATK || it[0].id == P_BATK) graph = 0;
        else if (it[0].id == P_FATK || it[0].id == P_EATK) graph = 1;
        else if (it[0].id == P_FREQ) graph = 2;      /* the filter's curve [TONIC] */
    }
    for (k = 0; k < 4; k++) {
        if (graph == 0 && k < 3) continue;           /* SYN2: ATK DEC END as one picture, LEV on its own */
        if (graph >= 1) continue;
        o_slot(&it[k], x0 + k * pitch, y, pitch, active);
        o_ink = K_FG;
    }
    if (graph >= 0) {
        int n = graph == 0 ? 3 : 4, j;
        if (graph == 0) {
            int b = it[0].id == P_AATK ? P_AATK : P_BATK;
            o_env(x0, y + 1, 3 * pitch - 2, 16, s->p[b], s->p[b + 1], s->p[b + 2], 0, 0, b == P_AATK ? 0 : 1);
            o_ts(x0 + 2, y + 3, b == P_AATK ? "A" : "B", 1);
        } else if (graph == 2) {
            float fs = (float)s->p[P_FREQ] * 0.25f, rs = (float)s->p[P_RESO] * 0.25f, fn, rn;
            if (!engine_filter_view(ui.trk, &fn, &rn))
                fn = fs, rn = rs;
            o_filter(x0, y + 1, 4 * pitch - 2, 16, s->p[P_FTYP], fn, fs, rn);
        } else if (it[0].id == P_FATK)
            o_env(x0, y + 1, 4 * pitch - 2, 16, s->p[P_FATK], s->p[P_FDEC], s->p[P_FSUS], s->p[P_FREL], 1, 2);
        else
            o_env(x0, y + 1, 4 * pitch - 2, 16, s->p[P_EATK], s->p[P_EDEC], s->p[P_ESUS], s->p[P_EREL], 1, 3);
        for (j = 0; j < n; j++) {                    /* the names under the picture */
            const char *nm = item_name(&it[j]);
            char t[8];
            int q;
            for (q = 0; q < 5 && nm[q]; q++)
                t[q] = nm[q];
            t[q] = 0;
            if (active) {
                o_rect(x0 + j * pitch - 1, y + 19, pitch, 7, 1);
                o_ts_c(x0 + j * pitch - 1, pitch, y + 20, t, 0);
            } else
                o_ts_c(x0 + j * pitch - 1, pitch, y + 20, t, 1);
        }
    }
}

/* ------------------------------------------------------------------ the piano roll --- */
/* the active track's notes: steps across (16, 32 or 64), pitch up; a bar a note, as long as it sounds, chords
 * stacked, micro timing shifting it; beats dotted, the Cs marked, lock trigs as ticks, the playhead a line */
static int roll_len_steps(const trig_t *g)          /* the note's length in 1/96 steps; -1 INF */
{
    int li = g->len != 0xFF ? g->len : ttrk()->tp[P_TLEN - NSP];
    return li >= LEN_INF ? -1 : (int)LEN_TICKS[li < NLEN ? li : 14];
}
/* text at full-resolution coordinates (the roll's labels) */
static void h_ts(int X, int Y, const char *s, int on)
{
    for (; *s; s++, X += 6) {
        const uint8_t *g = OF_S9[o_glyph_i((uint8_t)*s)];
        int r, c;
        for (r = 0; r < 9; r++)
            for (c = 0; c < 5; c++)
                if (g[r] >> (4 - c) & 1u)
                    h_px(X + c, Y + r, on);
    }
}
static int roll_black(int nt) { int k = nt % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
static int roll_val(int t, int st, int p)                   /* a trig parameter of a step: its lock, else the track's */
{
    int16_t v;
    return lock_get(pcur, t, st, p, &v) ? v : pcur->trk[t].tp[p - NSP];
}
/* the piano roll, at full resolution [TONIC 0.4]: a keyboard at the left (the notes sounding now lit), lanes shaded
 * for the black keys and lined at each C, bars and beats; the notes as rounded bars in the track's colour (sounding:
 * the accent; the held step: the accent, framed; soft: hollow; a retrig's repeats cut into the bar; a condition or
 * locks: a dot), a velocity lane under them, the playhead gliding, the steps past the track's end shaded */
static void o_roll(int y0, int y1)
{
    static const uint8_t RT_DIV_UI[17] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48, 64, 80};
    const track_t *k = ttrk();
    const int KX = 28, XE = HW - 1, Y0 = 2 * y0, VL = 12, YB = 2 * y1 - VL - 3;
    int t = ui.trk, len = k->len, n = 16 << ui.roll_zoom, s0, lo, hi, span, rh, rows, s, r, dr = drum_trk();
    float cw, ph = -1.0f;
    uint32_t lit[4] = {0, 0, 0, 0};                 /* the pitches sounding now (from the pattern, at the playhead) */
    if (n > 64) n = 64;
    if (n > len && ui.roll_zoom) n = len < 16 ? 16 : len;
    s0 = n == 16 ? ui.spage * 16 : 0;
    cw = (float)(XE - KX) / (float)n;
    if (engine_step(t) >= 0)                         /* the playhead, between steps too */
        ph = seq_pos(t) / 24.0f;
    /* the pitches shown: AUTO fits the track's notes (with a margin), else from VIEW up */
    lo = 127; hi = 0;
    for (s = 0; s < len; s++) {
        const trig_t *g = &pcur->trig[t][s];
        int j;
        if (!(g->flags & TF_NOTE))
            continue;
        for (j = 0; j < (g->nnote ? g->nnote : 1); j++) {
            int nt = g->note + (j ? g->chord[j - 1] : 0), l = roll_len_steps(g);
            if (nt < lo) lo = nt;
            if (nt > hi) hi = nt;
            if (dr) {                                /* a kit: its pads, a hit each */
                l = 48;
                nt = pad_of(nt);
            }
            if (ph >= 0 && ph >= (float)s && (l < 0 || ph < (float)s + (float)l / 96.0f) && nt >= 0 && nt < 128)
                lit[nt >> 5] |= 1u << (nt & 31);
        }
    }
    if (lo > hi) { lo = 48; hi = 72; }
    span = hi - lo + 1;
    rh = clamp((YB - Y0) / (span + 2), 3, 10);
    rows = (YB - Y0) / rh;
    if (ui.roll_lo >= 0)
        lo = ui.roll_lo;
    else
        lo = clamp(lo - (rows - span) / 2, 0, 127 - rows + 1);
    if (dr) {                                        /* a kit: a lane a pad, its model's name at the left */
        int cp = pad_of(k->tp[P_NOTE - NSP]);
        lo = 0;
        rows = NPAD;
        rh = (YB - Y0) / NPAD;
        for (r = 0; r < NPAD; r++) {
            int y = YB - (r + 1) * rh, on = lit[0] >> r & 1u, x;
            o_ink = K_DIM;
            for (x = KX + 2; x < XE; x += 4)
                h_px(x, y + rh - 1, 1);
            o_ink = on ? K_ACC : K_DIM;
            h_rect(17, y + 1, 9, rh - 2, 1);
            o_ink = r == cp ? K_ACC : K_FG;
            h_ts(1, y + (rh - 9) / 2, DM_SHORT[tsnd()->s[S_DM1 + r] % NDM], 1);
        }
    }
    /* the lanes and the keyboard */
    for (r = 0; r < rows && !dr; r++) {
        int nt = lo + r, y = YB - (r + 1) * rh, on = nt < 128 && (lit[nt >> 5] >> (nt & 31) & 1u), x;
        if (nt > 127)
            break;
        if (roll_black(nt)) {
            o_ink = K_DIM;
            for (x = KX + 2; x < XE; x += 4)
                h_px(x, y + rh / 2, 1);
            o_ink = on ? K_ACC : K_DIM;
            h_rect(17, y, 7, rh - 1, 1);
        } else {
            o_ink = on ? K_ACC : K_FG;
            h_rect(17, y, 10, rh - 1, 1);
        }
        if (nt % 12 == 0) {                          /* each C: a line across, its name */
            char nm[6];
            o_ink = K_DIM;
            for (x = KX; x < XE; x++)
                h_px(x, y + rh - 1, 1);
            note_name(nt, nm);
            o_ink = K_FG;
            h_ts(0, y + rh - 9 < Y0 ? Y0 : y + rh - 9, nm, 1);
        }
    }
    /* bars, beats, the end of the track */
    for (s = 0; s <= n; s++) {
        int x = KX + (int)((float)s * cw + 0.5f), y;
        if (s % 4 && cw < 6.0f)
            continue;
        o_ink = K_DIM;
        for (y = Y0; y <= YB; y += s % 16 == 0 ? 1 : s % 4 == 0 ? 2 : 6)
            h_px(x, y, 1);
    }
    if (s0 + n > len) {                              /* past the track's length: shaded */
        int x, y, xl = KX + (int)((float)(len - s0) * cw + 0.5f);
        o_ink = K_DIM;
        for (y = Y0; y <= YB; y += 3)
            for (x = xl + (y / 3 & 1) * 2; x < XE; x += 4)
                h_px(x, y, 1);
    }
    /* the velocity lane's base */
    o_ink = K_DIM;
    for (s = KX; s < XE; s += 2)
        h_px(s, 2 * y1 - 2, 1);
    /* the notes */
    for (s = 0; s < n; s++) {
        int st = s0 + s, j, xs;
        const trig_t *g;
        if (st >= len)
            break;
        g = &pcur->trig[t][st];
        xs = KX + (int)(((float)s + (float)g->micro / 24.0f) * cw + 0.5f);
        if ((g->flags & TF_LOCK) && !(g->flags & TF_NOTE)) {   /* a lock trig: a tick in the velocity lane */
            o_ink = K_DIM;
            h_rect(xs, 2 * y1 - 6, 2, 4, 1);
        }
        if (!(g->flags & TF_NOTE))
            continue;
        {
            int vel = g->vel ? g->vel : roll_val(t, st, P_TVEL), vh = vel * (VL - 1) / 127, held = ui.held >> st & 1u;
            o_ink = held ? K_ACC : K_TRK[t];         /* the velocity: a stalk with a head */
            h_rect(xs, 2 * y1 - 2 - vh, 1, vh, 1);
            h_rect(xs - 1, 2 * y1 - 3 - vh, 3, 2, 1);
        }
        for (j = 0; j < (g->nnote ? g->nnote : 1); j++) {
            int nt = g->note + (j ? g->chord[j - 1] : 0), l = roll_len_steps(g), w, y, h = rh - 1;
            int held = ui.held >> st & 1u, vel = g->vel ? g->vel : roll_val(t, st, P_TVEL), sounding;
            if (dr) {                                /* a kit: the pad's lane, a short block a hit */
                nt = pad_of(nt);
                l = 48;
            }
            sounding = ph >= 0 && ph >= (float)st && (l < 0 || ph < (float)st + (float)l / 96.0f);
            if (nt < lo || nt >= lo + rows)
                continue;
            w = l < 0 ? XE - xs : (int)((float)l * cw / 96.0f + 0.5f) - 1;
            if (dr && w < 3) w = 3;
            if (xs + w > XE) w = XE - xs;
            if (w < 2) w = 2;
            y = YB - (nt - lo + 1) * rh;
            o_ink = held || sounding ? K_ACC : K_TRK[t];
            if (vel >= 64) {
                h_rect(xs, y, w, h, 1);
                if (h >= 4 && w >= 4) {              /* rounded */
                    h_px(xs, y, 0); h_px(xs + w - 1, y, 0); h_px(xs, y + h - 1, 0); h_px(xs + w - 1, y + h - 1, 0);
                }
            } else {                                 /* soft: hollow */
                h_rect(xs, y, w, 1, 1); h_rect(xs, y + h - 1, w, 1, 1);
                h_rect(xs, y, 1, h, 1); h_rect(xs + w - 1, y, 1, h, 1);
            }
            if (held) {                              /* the held step: framed */
                int i;
                for (i = -2; i < w + 2; i++) { h_px(xs + i, y - 2, 1); h_px(xs + i, y + h + 1, 1); }
                for (i = -2; i < h + 2; i++) { h_px(xs - 2, y + i, 1); h_px(xs + w + 1, y + i, 1); }
            }
            if (roll_val(t, st, P_RTRG) && vel >= 64) {   /* the retrig's repeats: cuts in the bar */
                float iv = 16.0f / (float)RT_DIV_UI[clamp(roll_val(t, st, P_RTIM), 0, 16)] * cw, c;
                if (iv >= 3.0f)
                    for (c = iv; c < (float)w - 1.0f; c += iv)
                        h_rect(xs + (int)(c + 0.5f), y + 1, 1, h - 2 > 0 ? h - 2 : 1, 0);
            }
            if (!j && (g->cond || (g->flags & (TF_PLOCK | TF_SLOCK)))) {   /* a condition or locks: a dot above */
                o_ink = K_ACC;
                h_rect(xs, y - 3, 2, 2, 1);
            }
        }
    }
    if (ph >= (float)s0 && ph < (float)(s0 + n)) {  /* the playhead: a line and a marker on top */
        int x = KX + (int)((ph - (float)s0) * cw + 0.5f), y;
        o_ink = K_ACC;
        for (y = Y0; y <= 2 * y1 - 1; y++)
            h_px(x, y, 1);
        h_rect(x - 2, Y0, 5, 1, 1);
        h_rect(x - 1, Y0 + 1, 3, 1, 1);
    }
    if (n > 16) {                                    /* the page the keys reach */
        o_ink = K_ACC;
        h_rect(KX + (int)((float)(ui.spage * 16) * cw + 0.5f), Y0 - 3, (int)(16.0f * cw), 2, 1);
    }
    o_ink = K_FG;
}

/* the euclidean generator's rhythm [TONIC 0.6]: its LEN steps round a circle from the top, the hits as dots joined
 * into their polygon, the step playing now in the accent colour */
static void o_euclid(float cx, float cy, float r)
{
    const uint8_t *e = euc_of();
    int n = e[EU_PAT_] && e[EU_PAT_] < NRHY ? RHY[e[EU_PAT_]].len : e[EU_LEN_] ? e[EU_LEN_] : 16, i, first = -1, prev = -1;
    int play = engine_step(ui.trk);
    float px = 0, py = 0, fx = 0, fy = 0;
    for (i = 0; i < n; i++) {
        float a = (float)i / (float)n, x = cx + r * fm_sin_turns(a), y = cy - r * fm_sin_turns(a + 0.25f);
        if (euc_hit(e, i)) {
            o_ink = K_DIM;
            if (prev >= 0)
                o_fline(px, py, x, y);
            else
                first = i, fx = x, fy = y;
            prev = i;
            px = x;
            py = y;
        }
    }
    if (first >= 0 && prev != first) {
        o_ink = K_DIM;
        o_fline(px, py, fx, fy);
    }
    for (i = 0; i < n; i++) {
        float a = (float)i / (float)n, x = cx + r * fm_sin_turns(a), y = cy - r * fm_sin_turns(a + 0.25f);
        int now = play >= 0 && play % n == i;
        o_ink = now ? K_ACC : euc_hit(e, i) ? (euc_acc(e, i) ? K_ACC : K_TRK[ui.trk]) : K_DIM;
        if (euc_hit(e, i) || now) {
            o_dot(x, y);
            if (euc_hit(e, i) && euc_acc(e, i))      /* an accent: a ring round the dot */
                for (float a2 = 0; a2 < 1.0f; a2 += 0.125f)
                    h_px((int)(x * 2.0f + 0.5f + 4.0f * fm_sin_turns(a2)), (int)(y * 2.0f + 0.5f + 4.0f * fm_sin_turns(a2 + 0.25f)), 1);
        }
        else
            h_rect((int)(x * 2.0f + 0.5f) - 1, (int)(y * 2.0f + 0.5f) - 1, 2, 2, 1);
    }
    {
        char b[12];
        if (e[EU_PAT_] && e[EU_PAT_] < NRHY)
            tx_cpy(b, L_RHY[e[EU_PAT_]]);
        else {
            tx_num(b, e[EU_PULS_]);
            tx_cat(b, "/");
            tx_num(b + tx_len(b), n);
        }
        o_ink = K_FG;
        o_ts((int)(cx + r) + 6, (int)cy - 2, b, 1);
        if (drum_trk()) {
            o_ink = K_DIM;
            o_ts((int)(cx + r) + 6, (int)cy + 6, pad_label(cur_pad()), 1);
        }
    }
    o_ink = K_FG;
}
/* a kit's beat at a glance [TONIC 0.8]: the step page, a lane a pad (pad 1 at the bottom), the picked pad's lane in
 * the accent colour, the playhead */
static void o_euc_kit(int x0, int y0)
{
    int p, st, base = ui.spage * 16, play = engine_step(ui.trk), cp = cur_pad();
    for (p = 0; p < NPAD; p++) {
        int y = (y0 + (NPAD - 1 - p) * 2) * 2;
        for (st = 0; st < 16; st++) {
            const trig_t *g = &pcur->trig[ui.trk][(base + st) % NSTEP];
            int x = (x0 + st * 5) * 2, on = base + st < ttrk()->len && trig_pad(g, p) >= 0;
            o_ink = base + st == play ? K_ACC : on ? (p == cp ? K_ACC : K_TRK[ui.trk]) : K_DIM;
            if (on || base + st == play)
                h_rect(x, y, 8, 3, 1);
            else
                h_px(x + 4, y + 1, 1);
        }
    }
    o_ink = K_FG;
}
/* the master compressor's gain reduction [TONIC 0.6]: a bar to 24 dB */
static void o_gr(int x, int y, int w)
{
    char b[12];
    float gr = engine_comp_gr();
    int f = (int)(gr * (float)(w - 2) / 24.0f + 0.5f);
    o_ink = K_DIM;
    o_ts(x, y, "GR", 1);
    o_frame(x + 10, y, w, 5);
    o_ink = K_ACC;
    if (f > 0)
        o_rect(x + 11, y + 1, f > w - 2 ? w - 2 : f, 3, 1);
    tx_num(b, -(int)(gr + 0.5f));
    tx_cat(b, " DB");
    o_ink = K_FG;
    o_ts(x + w + 13, y, b, 1);
}

/* [0.10] the ducking's curve: the gain over a division (or a quarter, from a track) from DPTH, ATK and REL, the
 * place in it now, and the gain now as a bar */
static void o_duck(int x, int y, int w, int h)
{
    const int16_t *fx = pcur->fx;
    float ph, len, g = engine_duck(&ph, &len), d = (float)fx[P_SCDEP - P_CDEP] * (1.0f / 127.0f);
    float atk = fx[P_SCATK - P_CDEP] > 0 ? 0.002f + (float)fx[P_SCATK - P_CDEP] * (0.248f / 127.0f) : 0.0f;
    float tau = 0.01f * fm_exp2f((float)fx[P_SCREL - P_CDEP] * (7.0f / 127.0f));
    int i, py = y, src = fx[P_SCSRC - P_CDEP];
    if (ph < 0.0f)
        len = 60.0f / fm_maxf((float)pcur->bpm10 * 0.1f, 20.0f);
    o_ink = K_DIM;
    o_hline(x, y, w, 1);
    o_hline(x, y + h, w, 1);
    o_ink = src ? K_ACC : K_DIM;
    for (i = 0; i <= w; i++) {
        float t = (float)i / (float)w * len, e = atk > 0.0f && t < atk ? t / atk :
                  fm_expf(-(t - (atk > 0.0f ? atk : 0.0f)) / tau);
        int yy = y + (int)(d * e * (float)h + 0.5f);
        if (i)
            o_line(x + i - 1, py, x + i, yy);
        py = yy;
    }
    if (ph >= 0.0f) {                                /* now */
        o_ink = K_FG;
        o_vline(x + (int)(ph * (float)w), y, h + 1, 1);
    }
    o_ink = K_DIM;                                   /* the gain now */
    o_frame(x + w + 4, y, 4, h + 1);
    o_ink = K_ACC;
    if (g < 0.999f)
        o_rect(x + w + 5, y + 1, 2, (int)((1.0f - g) * (float)(h - 1) + 0.5f), 1);
}

static void o_page(void)
{
    const row_t *r = cur_row();
    int gi, row0, j, syn = ui.grp == G_SYN, x0 = syn ? 45 : 18, pitch = syn ? 19 : 25;
    char right[6] = "";
    char name[24];
    if (!r)
        return;
    gi = ui.row[ui.grp];
    row0 = gi > 0 && grp_rows(ui.grp)[gi - 1].title == r->title ? gi - 1 : gi;
    {                                                /* the page's place in its group, as "(1/2)" */
        int i, n = 0, me = 0;
        const char *last = 0;
        for (i = 0; i < grp_n(ui.grp); i++)
            if (grp_rows(ui.grp)[i].title != last) {
                last = grp_rows(ui.grp)[i].title;
                n++;
                if (i <= row0) me = n;
            }
        if (n > 1) {
            right[0] = (char)('0' + me % 10); right[1] = '/'; right[2] = (char)('0' + n % 10); right[3] = 0;
            if (n >= 10) { tx_num(right, me); tx_cat(right, "/"); tx_num(right + tx_len(right), n); }
        }
    }
    name[0] = 'T'; name[1] = (char)('1' + ui.trk); name[2] = ' '; name[3] = 0;
    tx_cat(name, ui.grp == G_FX || ui.grp == G_GLO ? r->title : tsnd()->name[0] ? tsnd()->name : "-");
    o_header(name, right);
    o_tabs(r);
    if (is_roll(r)) {                                /* ROLL: the piano roll, its knobs' row under it */
        int k;
        o_roll(18, 84);
        for (k = 0; k < 4; k++) {
            o_slot(&r->it[k], 1 + k * 30, 85, 30, 1);
            o_ink = K_FG;
        }
        return;
    }
    o_meters(19, 42);
    if (syn && tsnd()->s[S_ENGINE] == ENG_VA)
        o_va(18, 20, tsnd());
    else if (syn && drum_trk())
        o_kit(18, 20, tsnd());
    else if (syn)
        o_algo(18, 20, tsnd()->p[P_ALGO], tsnd()->p[P_HARM]);
    for (j = 0; j < 2; j++) {
        const row_t *rr = &grp_rows(ui.grp)[row0 + j];
        if (row0 + j >= grp_n(ui.grp) || (j && rr->title != r->title))
            break;
        o_row(rr, x0, pitch, 18 + j * 26, row0 + j == gi);
    }
    if (r->it[0].kind == IK_EUC && r->it[0].id >= EU_KIT0_)
        o_euc_kit(19, 45);
    else if (r->it[0].kind == IK_EUC && r->it[0].id != EU_NMODE_)
        o_euclid(46.0f, 54.0f, 8.5f);
    else if (r->it[0].kind == IK_FX && r->it[0].id == P_CPAMT)
        o_gr(20, 50, 60);
    else if (r->it[0].kind == IK_FX && (r->it[0].id == P_SCSRC || r->it[0].id == P_SCPAD))
        o_duck(22, 45, 80, 14);
}

/* ------------------------------------------------------- the keys' part (64..119) --- */
static void o_steps(int y)
{
    int s, t = ui.trk, page = ui.spage, play = engine_step(t), len = pcur->trk[t].len;
    for (s = 0; s < 16; s++) {
        int st = page * 16 + s, x = 1 + s * 7 + (s / 4), on;
        const trig_t *g = &pcur->trig[t][st];
        if (st >= len)
            continue;
        on = play == st;
        if ((g->flags & TF_NOTE) && drum_trk() && trig_pad(g, cur_pad()) < 0) {   /* a kit: other pads' steps */
            o_frame(x, y, 6, 6);
            if (on) o_rect(x + 1, y + 1, 4, 4, 1);
        } else if (g->flags & TF_NOTE) {
            o_rect(x, y, 6, 6, 1);
            if (on) o_rect(x + 2, y + 2, 2, 2, 0);
        } else if (g->flags & TF_LOCK) {
            o_frame(x, y, 6, 6);
            o_rect(x + 2, y + 2, 2, 2, 1);
            if (on) o_rect(x + 1, y + 1, 4, 4, 1);
        } else {
            if (on) o_rect(x, y, 6, 6, 1);
            else o_rect(x + 2, y + 2, 2, 2, 1);
        }
        if (g->flags & (TF_PLOCK | TF_SLOCK))      /* locks: a dot above; a condition: two */
            o_px(x + 2, y - 2, 1);
        if (g->cond)
            o_px(x + 4, y - 2, 1);
        if (ui.held >> st & 1u)
            o_hline(x, y + 7, 6, 0);
        if (ui.steprec && st == ui.step_cur)
            o_hline(x, y + 8, 6, 1);
    }
}

static void o_keys_roll(void)
{
    char b[12];
    int t, x;
    for (t = 0; t < NTRK; t++) {
        x = 1 + t * 15;
        b[0] = 'T'; b[1] = (char)('1' + t); b[2] = 0;
        o_ink = K_TRK[t];
        if (t == ui.trk) { o_box(x, 111, 13, 9); o_ts(x + 3, 113, b, 0); }
        else { o_frame(x, 111, 13, 9); o_ts(x + 3, 113, b, 1); }
        if (eng.mute >> t & 1u) o_line(x + 1, 118, x + 11, 112);
    }
    o_ink = K_FG;
    tx_fix(b, (int)(eng.bpm * 10.0f + 0.5f), 10, 1);
    o_tm(119 - o_wm(b, 1), 112, b, 1, 1);
    x = 119 - o_wm(b, 1) - 9;
    if (eng.playing) {
        int i;
        for (i = 0; i < 4; i++)
            o_vline(x + i, 113 + i, 7 - 2 * i, 0);
    } else
        o_rect(x, 113, 5, 5, 1);
}

static void o_keys(void)
{
    char b[24];
    int t, x;
    const char *mode = ui.perf ? "PERFORM" : eng.rec_live ? "LIVE REC" : ui.steprec ? "STEP REC" : ui.grid ? "GRID" : "KEYS";
    char mpad[24];
    if (!ui.perf && drum_trk()) {                    /* a kit: the picked pad beside the mode [TONIC 0.6.1] */
        tx_cpy(mpad, mode);
        tx_cat(mpad, " ");
        tx_cat(mpad, pad_label(cur_pad()));
        mode = mpad;
    }
    o_ink = K_DIM;
    o_hline(0, 71, OW, 1);
    o_ink = K_TRK[ui.trk];
    o_steps(75);
    o_ink = K_FG;
    /* the pages of steps, the mode */
    o_ts(1, 84, "PAGE", 1);
    for (t = 0; t < 4; t++)
        if (t * 16 < ttrk()->len) {
            if (t == ui.spage) o_rect(19 + t * 6, 84, 4, 5, 1);
            else o_frame(19 + t * 6, 84, 4, 5);
        }
    if (eng.rec_live && (plat_ms() / 300u) & 1u)
        o_rect(119 - o_ws(mode) - 5, 85, 3, 3, 1);
    o_ts(120 - o_ws(mode) - 1, 84, mode, 1);
    /* the tracks; the tempo and the transport */
    for (t = 0; t < NTRK; t++) {
        int m = eng.mute >> t & 1u;
        x = 1 + t * 15;
        b[0] = 'T'; b[1] = (char)('1' + t); b[2] = 0;
        o_ink = K_TRK[t];                            /* each track in its colour */
        if (t == ui.trk) {
            o_box(x, 91, 13, 9);
            o_ts(x + 3, 93, b, 0);
        } else {
            o_frame(x, 91, 13, 9);
            o_ts(x + 3, 93, b, 1);
        }
        if (m)                                       /* muted: struck through */
            o_line(x + 1, 98, x + 11, 92);
        o_ink = K_FG;
    }
    tx_fix(b, (int)(eng.bpm * 10.0f + 0.5f), 10, 1);
    o_tm(119 - o_wm(b, 1), 92, b, 1, 1);
    x = 119 - o_wm(b, 1) - 9;
    if (eng.playing) {                               /* play: a triangle; recording: a dot */
        int i;
        for (i = 0; i < 4; i++)
            o_vline(x + i, 93 + i, 7 - 2 * i, 0);
    } else
        o_rect(x, 93, 5, 5, 1);
    if (eng.ext_clock)
        o_ts(x - 13, 94, "EXT", 1);
    /* the held step, the step being entered, or a hint */
    b[0] = 0;
    if (ui.held || ui.steprec) {
        int st = ui.held ? first_held() : ui.step_cur, k;
        const trig_t *g = &pcur->trig[ui.trk][st];
        tx_cpy(b, "STEP ");
        tx_num(b + 5, st + 1);
        o_ts(1, 104, b, 1);
        if (g->flags & TF_NOTE) {
            char n[8];
            x = 34;
            for (k = 0; k < (g->nnote ? g->nnote : 1) && x < 110; k++) {
                note_name(g->note + (k ? g->chord[k - 1] : 0), n);
                x = o_tm(x, 103, n, 1, 1) + 4;
            }
        } else
            o_ts(34, 104, (g->flags & TF_LOCK) ? "LOCK TRIG" : "EMPTY", 1);
    } else if (ui.grp == G_TRIG || ui.grp == G_HOME || ui.grp == G_SYN || ui.grp == G_ARP) {
        /* a hint for where you are [0.9.1] */
        const row_t *r = cur_row();
        const char *h = ui.grid ? (drum_trk() ? "BLK8+KEY: PICK A PAD" : "HOLD A STEP: KNOBS LOCK IT") :
                        "REC GRID   REC+PLAY LIVE";
        if (r && r->it[0].kind == IK_EUC)
            h = "TURNS WRITE  SCL+HOME: UNDO";
        else if (ui.grp == G_SYN && drum_trk())
            h = "KEYS 1-8: THE PADS  OCT: PAD";
        else if (ui.grp == G_SYN)
            h = "K4 ON THE LAST ROW: ENGINE";
        else if (ui.grp == G_ARP)
            h = "CHORD WORKS WITHOUT THE ARP";
        o_ink = K_DIM, o_ts(1, 104, h, 1);
    }
    else if (ui.grp == G_LFO) {                      /* the LFO's value now */
        const row_t *r = cur_row();
        float v = engine_lfo_view(ui.trk, r && r->title[3] == '2' ? 1 : 0);
        int c = 60 + (int)(v * 58.0f);
        o_ts(1, 104, "LFO", 1);
        o_ink = K_DIM;
        o_hline(16, 106, 103, 1);
        o_ink = K_ACC;
        o_rect(c - 1, 103, 3, 7, 1);
        o_ink = K_FG;
    }
    if (ui.grp == G_ARP) {                           /* the arpeggiator's steps */
        const sound_t *s = tsnd();
        int k;
        for (k = 0; k < 16; k++) {
            int on = s->arp_on >> k & 1u, o = s->arp_ofs[k];
            x = 1 + k * 7 + (k / 4);
            if (k >= s->s[S_ARPSTEPS]) continue;
            o_hline(x, 117, 6, 1);
            if (on) o_rect(x, 113 - clamp(o, -2, 4), 6, 2, 1);
            if (k == ui.arp_step) o_hline(x, 119, 6, 0);
        }
    } else {
        o_ink = K_DIM;
        tx_cpy(b, "SWING ");
        tx_num(b + 6, pcur->swing);
        tx_cat(b, "%");
        o_ts(1, 113, b, 1);
        if (set.chain_n > 1)
            o_ts(44, 113, "CHAIN", 1);
        if (store_unsaved())
            o_ts(120 - o_ws("UNSAVED") - 1, 113, "UNSAVED", 1);
    }
}

/* ------------------------------------------------------------------ other screens --- */
static void o_home(void)
{
    char b[24];
    int t;
    o_header(pcur->name[0] ? pcur->name : "UNTITLED", "");
    pat_name(cur_idx, b);
    o_tm(0, 12, b, 1, 2);
    tx_cpy(b, "CPU ");                               /* the render's share of its time (the platform measures it) */
    tx_num(b + 4, (int)plat_cpu_pct());
    tx_cat(b, "%");
    if (plat_xruns()) {
        tx_cat(b, " XR ");
        tx_num(b + tx_len(b), (int)plat_xruns());
    }
    o_ts(120 - o_ws(b) - 1, 13, b, 1);
    o_ts(40, 13, "HOME", 1);
    o_ts(40, 21, "K1-4 LEVEL SEL BPM", 1);
    for (t = 0; t < NTRK; t++) {
        int y = 32 + t * 8, lvl = pcur->trk[t].tp[P_TLVL - NSP], m = eng.mute >> t & 1u, meter;
        char n[16];
        int k;
        b[0] = 'T'; b[1] = (char)('1' + t); b[2] = 0;
        o_ink = K_TRK[t];                            /* each track in its colour */
        if (t == ui.trk) { o_rect(0, y - 1, 11, 7, 1); o_ts(2, y, b, 0); }
        else o_ts(2, y, b, 1);
        for (k = 0; k < 12 && pcur->snd[t].name[k]; k++)
            n[k] = pcur->snd[t].name[k];
        n[k] = 0;
        o_ts(14, y, n, 1);
        o_frame(64, y, 32, 5);
        o_rect(65, y + 1, lvl * 30 / 127, 3, 1);
        meter = clamp((int)(engine_level(t) * 60.0f), 0, 20);
        for (k = 0; k < meter; k += 2)
            o_rect(99 + k, y, 1, 5, 1);
        if (m)
            o_ts(100, y, "MUTE", 1);
        o_ink = K_FG;
    }
}

static void o_perf(void)
{
    static const char *const BL[11] = {"M1", "M2", "M3", "M4", "FILL", "FIL+", "BANK", "TAP", "TSAV", "RELD", "RST"};
    char b[8];
    int i;
    int fxm = (plat_buttons() >> B_SEL & 1u) || ui.pfx_keys;   /* SCL held or an effect on: the effects' keys */
    o_header("PERFORM", ui.perf_lock ? "LOCKED" : "");
    o_ts(0, 12, fxm ? "EFFECTS: HOLD A KEY" : "PATTERNS      SCL+KEY: EFFECTS", 1);
    for (i = 0; fxm && i < 16; i++) {                /* the momentary effects [TONIC 0.7] */
        static const char *const FN[NPFX] = {"1/4", "1/8", "1/16", "1/32", "HALF", "TAPE", "LP", "HP", "FRZ"};
        int x = (i % 8) * 15, y = 19 + (i / 8) * 11;
        if (i >= NPFX) {
            o_ink = K_DIM;
            o_frame(x, y, 14, 9);
            continue;
        }
        o_ink = i < 4 ? K_FG : K_ACC;
        if (ui.pfx_keys >> i & 1u) { o_box(x, y, 14, 9); o_ts_c(x, 14, y + 2, FN[i], 0); }
        else { o_ink = K_FG; o_frame(x, y, 14, 9); o_ts_c(x, 14, y + 2, FN[i], 1); }
    }
    o_ink = K_FG;
    for (i = 0; !fxm && i < 16; i++) {
        int idx = (cur_idx / 16) * 16 + i, x = (i % 8) * 15, y = 19 + (i / 8) * 11;
        pat_name(idx, b);
        if (idx == cur_idx) { o_box(x, y, 14, 9); o_ts(x + 2, y + 2, b, 0); }
        else {
            o_frame(x, y, 14, 9);
            if (idx == next_idx) o_frame(x + 1, y + 1, 12, 7);
            o_ts(x + 2, y + 2, b, 1);
        }
    }
    for (i = 0; i < 11; i++) {
        int x = (i % 8) * 15, y = 50, on = (i < 4 && !(eng.mute >> i & 1u)) || (i == 4 && eng.fill);
        o_ink = i < 4 ? K_TRK[i] : i == 4 && eng.fill ? K_ACC : K_FG;
        if (!BL[i][0] || i >= 8)
            continue;
        if (on) { o_box(x, y, 14, 9); o_ts_c(x, 14, y + 2, BL[i], 0); }
        else { o_frame(x, y, 14, 9); o_ts_c(x, 14, y + 2, BL[i], 1); }
    }
    o_ink = K_ACC;
    {   /* in place of the keys' label: K1 the DJ filter (a bar from the centre), K2 the ducking depth */
        item_t a = F_(P_MFLT), d = F_(P_SCDEP);
        int lk, v = item_get(&a, &lk);
        o_rect(0, 42, OW, 7, 0);
        o_ts(0, 43, "FLT", 1);
        o_frame(13, 42, 29, 7);
        o_rect(27, 43, 1, 5, 1);
        if (v < 0) o_rect(27 + v * 13 / 64, 44, -v * 13 / 64, 3, 1);
        else if (v > 0) o_rect(28, 44, v * 13 / 63, 3, 1);
        item_fmt(&a, v, b);
        o_ts(44, 43, v ? b : "OFF", 1);
        v = item_get(&d, &lk);
        o_ts(62, 43, "DCK", 1);
        o_frame(75, 42, 29, 7);
        if (pcur->fx[P_SCSRC - P_CDEP]) o_rect(77, 44, v * 25 / 127, 3, 1);
        item_fmt(&d, v, b);
        o_ts(106, 43, pcur->fx[P_SCSRC - P_CDEP] ? b : "OFF", 1);
    }
    o_ink = K_FG;
}

static const char *const SAVE_ROW[10] = {"SAVE PROJECT", "SOUND > LIBRARY", "SOUND > POOL", "POOL > TRACK",
                                         "INIT SOUND", "RELOAD PATTERN", "COPY PATTERN", "PASTE PATTERN",
                                         "CLEAR PATTERN", "LOAD DEMO PROJECT"};
static void o_save(void)
{
    char b[24];
    int i;
    o_header("SAVE", "");
    for (i = 0; i < 10; i++) {
        int y = 12 + i * 7, on = i == ui.save_row;
        if (on) { o_rect(0, y - 1, OW, 7, 1); o_ts(2, y, SAVE_ROW[i], 0); }
        else o_ts(2, y, SAVE_ROW[i], 1);
        if (i >= 1 && i <= 3) {                      /* the slot */
            int slot = i == 1 ? ui.save_slot % 32 : ui.save_slot % 16, k;
            const sound_t *s = i == 1 ? &lib[slot] : &pool[slot];
            tx_num(b, slot + 1);
            tx_cat(b, " ");
            k = tx_len(b);
            tx_cat(b, s->name[0] ? s->name : "EMPTY");
            b[k + 10 < 23 ? k + 10 : 23] = 0;
            o_ts(70, y, b, !on);
        }
    }
    o_hline(0, 84, OW, 1);
    o_ts(1, 88, ui.confirm ? "SAVE AGAIN: CONFIRM" : "SELECT ROW  PRESETS SLOT", 1);
    o_ts(1, 96, ui.confirm ? "OCT: CANCEL" : "SAVE: DO IT", 1);
    o_ts(1, 108, store_unsaved() ? "UNSAVED CHANGES" : "ALL SAVED", 1);
}

/* a message: a box in the middle, as the Digitone's pop-ups */
static void o_msg(void)
{
    int w = o_wm(ui.msg[0], 1), w2 = o_ws(ui.msg[1]), bw = (w > w2 ? w : w2) + 12;
    if (bw < 60) bw = 60;
    if (bw > OW) bw = OW;
    o_rect((OW - bw) / 2 - 1, 21, bw + 2, 24, 0);
    o_frame((OW - bw) / 2, 22, bw, 22);
    o_frame((OW - bw) / 2 + 1, 23, bw - 2, 20);
    o_tm((OW - w) / 2, 26, ui.msg[0], 1, 1);
    o_ts((OW - w2) / 2, 36, ui.msg[1], 1);
}

static void save_action(void)
{
    int r = ui.save_row, destructive = r == 5 || r == 8 || r == 9;
    if (destructive && !ui.confirm) {
        ui.confirm = 1;
        ui.redraw = 1;
        return;
    }
    ui.confirm = 0;
    if ((r == 5 || r == 7 || r == 8) && next_idx >= 0) {   /* (the next pattern waits in the buffer these use) [0.9.2] */
        app_say("PATTERN CHANGE", "WAIT FOR IT");
        return;
    }
    switch (r) {
    case 0:
        if (eng.playing)
            app_say("STOP", "TO SAVE");
        else
            app_say(store_flush() == 0 ? "SAVED" : "SAVE FAILED", "");
        break;
    case 1: lib[ui.save_slot % 32] = *tsnd(); lib_dirty = 1; app_say("TO LIBRARY", tsnd()->name); break;
    case 2: pool[ui.save_slot % 16] = *tsnd(); pool_dirty = 1; app_say("TO POOL", tsnd()->name); break;
    case 3:
        if (pool[ui.save_slot % 16].name[0]) {
            snd_bak = *tsnd();
            *tsnd() = pool[ui.save_slot % 16];
            if (!pattern_fits()) {                   /* [0.9.2] */
                *tsnd() = snd_bak;
                app_say("PATTERN", "FULL");
                break;
            }
            engine_sound_changed(ui.trk);
            mark_dirty();
            app_say("LOADED", tsnd()->name);
        }
        break;
    case 4: sound_init(tsnd()); engine_sound_changed(ui.trk); mark_dirty(); app_say("INIT", "SOUND"); break;
    case 5: {
        int k = dirty_find(cur_idx);
        if (k >= 0)
            dirty[k].len = 0;
        cur_dirty = 0;
        snap_idx = -1;
        pattern_temp_reload();
        cur_dirty = 0;
        app_say("RELOADED", "");
        break;
    }
    case 6: {                                        /* COPY: packed, in its own buffer [0.9.2] */
        int n = pattern_pack(pcur, copy_buf, PLAT_STORE_MAX);
        copy_len = (uint16_t)(n > 0 ? n : 0);
        app_say(n > 0 ? "COPIED" : "TOO BIG", "");
        break;
    }
    case 7:
        if (copy_len && pattern_unpack(pnext, copy_buf, copy_len) == 0) {
            engine_all_off(-1);
            eng_pat = pnext;
            {
                pattern_t *t = pcur;
                pcur = pnext;
                pnext = t;
            }
            mark_dirty();
            app_say("PASTED", "");
        } else
            app_say("NOTHING", "COPIED");
        break;
    case 8:
        engine_all_off(-1);
        pattern_init(pnext);
        pnext->snd[0] = pcur->snd[0]; pnext->snd[1] = pcur->snd[1];
        pnext->snd[2] = pcur->snd[2]; pnext->snd[3] = pcur->snd[3];
        eng_pat = pnext;
        {
            pattern_t *t = pcur;
            pcur = pnext;
            pnext = t;
        }
        mark_dirty();
        app_say("CLEARED", "");
        break;
    case 9:
        if (eng.playing) {
            app_say("STOP", "FIRST");
            break;
        }
        demo_write(0);
        store_boot();
        app_say("DEMO", "LOADED");
        break;
    }
    ui.redraw = 1;
}

/* the value just turned, big, over the lower part of the screen (for a second and a half) [TONIC] */
static void o_popup(void)
{
    const row_t *r = cur_row();
    const item_t *it;
    char v[24], n[16];
    int lk, val, k;
    if (!r || ui.touch_k > 3)
        return;
    it = &r->it[ui.touch_k];
    if (it->kind == IK_NONE)
        return;
    val = item_get(it, &lk);
    item_fmt(it, val, v);
    for (k = 0; k < 15 && item_name(it)[k]; k++)
        n[k] = item_name(it)[k];
    n[k] = 0;
    o_rect(0, 72, OW, 33, 0);
    o_frame(0, 72, OW, 33);
    o_ink = K_DIM;
    o_ts(4, 75, n, 1);
    o_ink = K_ACC;
    if (lk)
        o_ts(120 - o_ws("LOCK") - 4, 75, "LOCK", 1);
    o_ink = K_FG;
    while (o_wm(v, 2) > OW - 8 && v[0])
        v[tx_len(v) - 1] = 0;
    o_tm((OW - o_wm(v, 2)) / 2, 84, v, 1, 2);
}

/* PRESETS turned: a window with the list (factory sounds by group, then the library's filled slots), the cursor
 * inverted, the track's sound marked >, the cursor's group and place on top; OCT+ loads, OCT- closes [TONIC] */
static void o_plist(void)
{
    static uint8_t vis[NFACTORY + 32];
    int n = 0, i, cur = 0, top, rows = 10, x = 4, w = OW - 8, y0 = 9;
    char b[24];
    int loaded = sound_index();
    for (i = 0; i < NFACTORY + 32; i++)
        if (i < NFACTORY || lib[i - NFACTORY].name[0]) {
            if (i == ui.plist_i)
                cur = n;
            vis[n++] = (uint8_t)i;
        }
    top = cur - rows / 2;
    if (top > n - rows) top = n - rows;
    if (top < 0) top = 0;
    o_rect(x - 1, y0 - 1, w + 2, rows * 8 + 22, 0);
    o_frame(x - 1, y0 - 1, w + 2, rows * 8 + 22);
    o_rect(x, y0, w, 9, 1);                          /* the title: PRESETS, the group, the place */
    o_ts(x + 2, y0 + 2, "PRESETS", 0);
    tx_cpy(b, ui.plist_i < NFACTORY ? preset_group(ui.plist_i, 0) : "USER");
    tx_cat(b, " ");
    tx_num(b + tx_len(b), cur + 1);
    tx_cat(b, "/");
    tx_num(b + tx_len(b), n);
    o_ts(x + w - 2 - o_ws(b), y0 + 2, b, 0);
    for (i = 0; i < rows && top + i < n; i++) {
        int k = vis[top + i], y = y0 + 11 + i * 8, first = 0, sel = top + i == cur;
        const char *g = "";
        if (k < NFACTORY) {
            int f;
            g = preset_group(k, &f);
            first = f == k;
            tx_cpy(b, FACTORY[k].name);
        } else {
            b[0] = 'U';
            b[1] = (char)('0' + (k - NFACTORY + 1) / 10);
            b[2] = (char)('0' + (k - NFACTORY + 1) % 10);
            b[3] = ' ';
            tx_cpy(b + 4, lib[k - NFACTORY].name);
            g = "USER";
            first = top + i == 0 || vis[top + i - 1] < NFACTORY;
        }
        b[16] = 0;
        if (k == loaded)                             /* the track's sound now */
            o_ts(x + 1, y + 1, ">", !sel);
        if (first && i)                              /* a group starts: a dotted line above it */
            o_hline(x + 1, y - 1, w - 2, 1);
        if (sel) {
            o_rect(x + 1, y, w - 2, 7, 1);
            o_ts(x + 5, y + 1, b, 0);
            if (first) o_ts(x + w - 3 - o_ws(g), y + 1, g, 0);
        } else {
            o_ts(x + 5, y + 1, b, 1);
            if (first) o_ts(x + w - 3 - o_ws(g), y + 1, g, 1);
        }
    }
    o_hline(x + 1, y0 + 11 + rows * 8, w - 2, 0);
    o_ts(x + 3, y0 + 13 + rows * 8, "OCT+ LOAD  OCT- BACK", 1);
}

/* a button held alone for a second: what it does, alone and with the others [TONIC] */
static void o_help(int b)
{
    /* (0.9.1: up to date with 0.5 .. 0.9) */
    static const char *const H_SCL[] = {"SCL = THE DIGITONE'S FUNC", "+REC    STEP RECORDING", "+PLAY   STOP, CUT THE FX",
                                        "+SAVE   SAVE NOW", "+HOME   UNDO (AGAIN: REDO)", "+OCT    TRANSPOSE (KIT: PAD)",
                                        "+KNOB   FINE STEPS", "+PAGE   THE ROW BEFORE", "+WHITE  LOCK TRIG (GRID)",
                                        "+KEY1-4 TRACK (KEYS)", "+PRESET THE NEXT GROUP", "HOME+SCL+KEY  EFFECTS", 0};
    static const char *const H_PAGE[] = {"TAP AGAIN  NEXT ROW", "HOLD + KNOB  ALL TRACKS", "HOLD + REC   RANDOMIZE ROW",
                                         "HOLD + OCT-  ROW DEFAULTS", "HOLD + SCL   UNDO THAT", 0};
    static const char *const H_REC[] = {"REC   GRID RECORDING", "WHITE KEYS  THE STEPS", "BLK 1-4  STEP PAGES",
                                        "BLK 5 + WHITE  LOCK TRIG", "BLK 6/7  MICRO (STEP HELD)", "BLK 6/7  SHIFT TRACK",
                                        "BLK 8 + WHITE  KIT: PAD", "BLK 9 10 11  COPY PASTE CLR", "PAGE + WHITE  LENGTH",
                                        "REC+PLAY  LIVE RECORDING", "SCL+REC  STEP RECORDING", 0};
    static const char *const H_PLAY[] = {"PLAY  START / STOP", "REC+PLAY  LIVE RECORDING", "PLAY AGAIN  QUANTIZE ON/OFF",
                                         "SCL+PLAY  STOP, CUT THE FX", 0};
    static const char *const H_OCT[] = {"OCT  THE KEYBOARD OCTAVE", "KIT  THE PAD BEFORE/AFTER", "GRID  THE STEP PAGE",
                                        "STEP HELD  ITS NOTE", "SCL  TRANSPOSE (KIT: PAD)", "HOME HELD  TRANSPOSE PTN",
                                        "PAGE HELD + OCT-  DEFAULTS", "PRESET LIST: + LOAD - BACK", 0};
    static const char *const H_SAVE[] = {"SAVE  THE SAVE PAGE", "ON IT  SAVE DOES THE ROW", "SELECT ROW  PRESETS SLOT",
                                         "SCL+SAVE  SAVE NOW", "SAVED BY ITSELF WHEN", "STOPPED AND QUIET", 0};
    static const char *const H_FX[] = {"FX  CHORUS DELAY REVERB", "    MIXER: LEVELS OVD FILT", "    DUCK PUMP: SIDECHAIN",
                                       "    COMP: MASTER COMPRESSOR", "TAP AGAIN  NEXT ROW", "SCL+FX  THE ROW BEFORE", 0};
    static const char *const H_GLO[] = {"GLO  TEMPO SCALE VOICE", "     SETUP PORTA MOD SONG", "     KEYBOARD MIDI DISPLAY",
                                        "TAP AGAIN  NEXT ROW", 0};
    static const char *const H_ARP[] = {"ARP  THE ARPEGGIATOR", "MODE SPEED RANGE LENGTH", "STEPS AND THEIR OFFSETS",
                                        "CHORD  ON EACH NOTE, NO ARP", "  NEEDED: VOICE BASS STRUM", "SCALE  NOTES SNAP TO IT,",
                                        "  CHORDS BY DEGREES", 0};
    static const char *const H_SEQ[] = {"TRIG  NOTE VEL LEN PROB", "ROLL  THE PIANO ROLL", "RTRG  RETRIG / RATCHETS",
                                        "EUCLID  RHYTHMS: LEN HITS", "  ROT STYLE / ACC GHOST", "  NOTES (KIT: ALL PADS)",
                                        "TRACK  ITS LEN AND SCALE", 0};
    static const char *const H_EDIT[] = {"THE SOUND'S ENGINE:", "FM  SYN1 SYN2 (OPERATORS)", "VA  OSC ENV SHAPE",
                                         "DRUM  PADS LEVEL CTL KIT", "ENGINE  K4, THE LAST ROW", 0};
    static const char *const H_ENV[] = {"FLTR  FILTER, ITS ENVELOPE", "AMP  ENVELOPE DRIVE PAN", "AMP 2  FX SENDS",
                                        "LOFI  SAMPLE RATE, BITS", "INSRT  DIST FOLD CRUSH", 0};
    static const char *const H_LFO[] = {"LFO1 LFO2  SPEED DEST", "WAVE MODE DEPTH", "LFO2 ALSO MOVES LFO1",
                                        "DEST: ANY PARAMETER", "(KIT: PADS, ALL ...)",
                                        "MENV  AN ENVELOPE, A DEST", 0};
    const char *const *h = H_PAGE;
    const char *title = "", *first = 0;
    int i, y;
    switch (b) {
    case B_SEL: h = H_SCL; title = "SCL"; break;
    case B_REC: h = H_REC; title = "REC"; break;
    case B_PLAY: h = H_PLAY; title = "PLAY"; break;
    case B_OCTDN: case B_OCTUP: h = H_OCT; title = "OCT"; break;
    case B_SAVE: h = H_SAVE; title = "SAVE"; break;
    case B_FX: h = H_FX; title = "FX"; break;
    case B_GLO: h = H_GLO; title = "GLO"; break;
    case B_ARP: h = H_ARP; title = "ARP"; break;
    case B_SEQ: h = H_SEQ; title = "SEQ"; break;
    case B_EDIT: h = H_EDIT; title = "EDIT"; break;
    case B_ENV: h = H_ENV; title = "ENV"; break;
    case B_LFO: h = H_LFO; title = "LFO"; break;
    default: return;
    }
    o_clear();
    o_box(0, 0, OW, 9);
    o_tm(2, 1, title, 0, 1);
    o_tm(OW - o_wm("HELP", 1) - 2, 1, "HELP", 0, 1);
    y = 13;
    if (first) {
        o_ts(1, y, first, 1);
        y += 9;
    }
    for (i = 0; h[i] && y < 112; i++, y += 8)
        o_ts(1, y, h[i], 1);
    o_ts(1, 113, "LET GO TO RETURN", 1);
}

static void ui_draw(void)
{
    uint32_t now = plat_ms();
    static uint32_t last;
    static uint8_t msg_up, touched;
    int step = engine_step(ui.trk), touch = now - ui.touch_t < 1500u;
    if (ui.msg_until && now >= ui.msg_until && msg_up) {
        msg_up = 0;
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (touch != touched)                            /* the envelope pictures come back after a knob */
        ui.redraw = 1;
    touched = (uint8_t)touch;
    /* everything is drawn again when something changed, the step moved, or every 100 ms (meters, blinking,
     * the LFO); only the bands that differ go to the screen */
    if (!ui.redraw && (uint8_t)step == drawn_step && now - last < 100u)
        return;
    last = now;
    o_clear();
    if (ui.perf)
        o_perf();
    else if (ui.grp == G_HOME)
        o_home();
    else if (ui.grp == G_SAVE)
        o_save();
    else
        o_page();
    if (!ui.perf && ui.grp == G_TRIG && is_roll(cur_row()))
        o_keys_roll();
    else if (ui.grp != G_SAVE)
        o_keys();
    if (touch && !ui.perf && ui.grp != G_HOME && ui.grp != G_SAVE && !(ui.grp == G_TRIG && is_roll(cur_row())))
        o_popup();
    if (ui.plist_on)
        o_plist();
    if (ui.msg_until && now < ui.msg_until) {
        o_msg();
        msg_up = 1;
    }
    if (ui.help_btn < NB && !ui.help_block && (plat_buttons() >> ui.help_btn & 1u) && now - ui.btn_t[ui.help_btn] > 900u)
        o_help(ui.help_btn);
    o_send();
    drawn_step = (uint8_t)step;
    ui.redraw = 0;
}
