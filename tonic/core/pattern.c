/* SPDX-License-Identifier: MPL-2.0
 * TONIC patterns and sounds: initial values, the lock pool, the saved form. */
#include "pattern.h"

static void bytes_zero(void *d, uint32_t n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = 0; }
static void bytes_copy(void *d, const void *s, uint32_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; while (n--) *a++ = *b++; }

void sound_init(sound_t *s)
{
    int i, j;
    bytes_zero(s, sizeof *s);
    for (i = 0; i < NSP; i++)
        s->p[i] = PDESC[i].def;
    s->s[S_PSLOPE] = 1;                            /* constant time */
    s->s[S_PAMT] = 100;
    s->s[S_PLAYMODE] = PM_POLY;
    s->s[S_VEL2VOL] = VV_LIN;
    s->s[S_PBDEP] = 2;
    s->s[S_OCT] = 2;                               /* 0 octaves (stored +2) */
    s->s[S_ARPSPD] = 5;                            /* 1/16 */
    s->s[S_ARPRNG] = 1;
    s->s[S_ARPLEN] = 10;                           /* 0.75 steps */
    s->s[S_ARPSTEPS] = 16;
    for (i = 0; i < NMS; i++)
        for (j = 0; j < NMSLOT; j++)
            s->mdst[i][j] = 0;                     /* D_NONE */
    s->arp_on = 0xFFFFu;
    bytes_copy(s->name, "INIT", 5);
}

void track_init(track_t *t)
{
    int i;
    bytes_zero(t, sizeof *t);
    for (i = 0; i < NTP_END - NSP; i++)
        t->tp[i] = PDESC[NSP + i].def;
    t->len = 16;
    t->scale = 4;                                  /* 1X */
    t->unison = 1;
    t->uspread = 40;
}

void pattern_init(pattern_t *p)
{
    int t, i;
    bytes_zero(p, sizeof *p);
    p->magic = PAT_MAGIC;
    p->bpm10 = 1200;
    p->swing = 50;
    p->mlen = 16;
    for (i = 0; i < NFXP; i++)
        p->fx[i] = PDESC[P_CDEP + i].def;
    for (t = 0; t < NTRK; t++) {
        track_init(&p->trk[t]);
        sound_init(&p->snd[t]);
        for (i = 0; i < NSTEP; i++) {
            p->trig[t][i].len = 0xFF;
            p->trig[t][i].nnote = 1;
        }
    }
    for (i = 0; i < NLOCK; i++)
        p->lock[i].f.param = P_NONE;
}

int pattern_empty(const pattern_t *p)
{
    int t, s;
    for (t = 0; t < NTRK; t++)
        for (s = 0; s < NSTEP; s++)
            if (p->trig[t][s].flags & (TF_NOTE | TF_LOCK))
                return 0;
    return 1;
}

/* ------------------------------------------------------------------------- locks --- */
int lock_get(const pattern_t *p, int trk, int step, int param, int16_t *val)
{
    int i;
    uint8_t key = LOCK_KEY(trk, step);
    for (i = 0; i < p->nlock; i++)
        if (p->lock[i].f.param == param && p->lock[i].f.step == key) {
            if (val)
                *val = p->lock[i].f.val;
            return 1;
        }
    return 0;
}

int lock_count(const pattern_t *p, int trk, int step)
{
    int i, n = 0;
    uint8_t key = LOCK_KEY(trk, step);
    for (i = 0; i < p->nlock; i++)
        n += p->lock[i].f.param != P_NONE && p->lock[i].f.step == key;
    return n;
}

void trig_flags_update(pattern_t *p, int trk, int step)
{
    int i, pl = 0, sl = 0;
    uint8_t key = LOCK_KEY(trk, step);
    for (i = 0; i < p->nlock; i++)
        if (p->lock[i].f.param != P_NONE && p->lock[i].f.step == key) {
            if (p->lock[i].f.param == P_SOUND)
                sl = 1;
            else
                pl = 1;
        }
    p->trig[trk][step].flags = (uint8_t)((p->trig[trk][step].flags & ~(TF_PLOCK | TF_SLOCK)) |
                                         (pl ? TF_PLOCK : 0) | (sl ? TF_SLOCK : 0));
}

/* an entry is written whole (one 32-bit store): the render never sees half of one */
int lock_set(pattern_t *p, int trk, int step, int param, int val)
{
    int i, fr = -1;
    uint8_t key = LOCK_KEY(trk, step);
    plock_t l;
    for (i = 0; i < p->nlock; i++) {
        if (p->lock[i].f.param == param && p->lock[i].f.step == key) {
            l = p->lock[i];
            l.f.val = (int16_t)val;
            p->lock[i].w = l.w;
            return 0;
        }
        if (fr < 0 && p->lock[i].f.param == P_NONE)
            fr = i;
    }
    if (fr < 0) {
        if (p->nlock >= NLOCK)
            return -1;
        fr = p->nlock;
    }
    l.f.step = key;
    l.f.param = (uint8_t)param;
    l.f.val = (int16_t)val;
    p->lock[fr].w = l.w;
    if (fr == p->nlock)
        p->nlock++;
    trig_flags_update(p, trk, step);
    return 0;
}

void lock_clear(pattern_t *p, int trk, int step, int param)
{
    int i;
    uint8_t key = LOCK_KEY(trk, step);
    for (i = 0; i < p->nlock; i++)
        if (p->lock[i].f.step == key && p->lock[i].f.param != P_NONE && (param == P_NONE || p->lock[i].f.param == param)) {
            plock_t l = p->lock[i];
            l.f.param = P_NONE;
            p->lock[i].w = l.w;
        }
    trig_flags_update(p, trk, step);
}

void lock_compact(pattern_t *p)
{
    int i, j = 0;
    for (i = 0; i < p->nlock; i++)
        if (p->lock[i].f.param != P_NONE)
            p->lock[j++].w = p->lock[i].w;
    for (i = j; i < p->nlock; i++)
        p->lock[i].f.param = P_NONE;
    p->nlock = (uint16_t)j;
}

/* -------------------------------------------------------------------- saved form --- */
typedef struct { uint8_t *b; int n, max, bad; } wr_t;
static void w8(wr_t *w, int v) { if (w->n < w->max) w->b[w->n] = (uint8_t)v; else w->bad = 1; w->n++; }
static void w16(wr_t *w, int v) { w8(w, v >> 8); w8(w, v); }
static void wbytes(wr_t *w, const void *s, int n) { const uint8_t *p = (const uint8_t *)s; while (n--) w8(w, *p++); }
typedef struct { const uint8_t *b; int n, at, bad; } rd_t;
static int r8(rd_t *r) { if (r->at < r->n) return r->b[r->at++]; r->bad = 1; return 0; }
static int r16(rd_t *r) { int h = r8(r); return (int16_t)(h << 8 | r8(r)); }
static void rbytes(rd_t *r, void *d, int n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)r8(r); }

static void put_sound(wr_t *w, const sound_t *s)
{
    sound_t z;
    int i, j, n = 0;
    sound_init(&z);
    for (i = 0; i < NSP; i++)
        n += s->p[i] != z.p[i];
    w8(w, n);
    for (i = 0; i < NSP; i++)
        if (s->p[i] != z.p[i]) {
            w8(w, i);
            w16(w, s->p[i]);
        }
    w8(w, NSS);
    wbytes(w, s->s, NSS);
    n = 0;
    for (i = 0; i < NMS; i++)
        for (j = 0; j < NMSLOT; j++)
            n += s->mdst[i][j] != 0;
    w8(w, n);
    for (i = 0; i < NMS; i++)
        for (j = 0; j < NMSLOT; j++)
            if (s->mdst[i][j]) {
                w8(w, i << 4 | j);
                w8(w, s->mdst[i][j]);
                w8(w, s->mdep[i][j]);
            }
    wbytes(w, s->arp_ofs, 16);
    w16(w, s->arp_on);
    wbytes(w, s->name, NAMELEN);
}

static void get_sound(rd_t *r, sound_t *s)
{
    int i, n, ns;
    sound_init(s);
    n = r8(r);
    for (i = 0; i < n; i++) {
        int id = r8(r), v = r16(r);
        if (id < NSP && v >= PDESC[id].lo && v <= PDESC[id].hi)
            s->p[id] = (int16_t)v;
    }
    ns = r8(r);
    for (i = 0; i < ns; i++) {
        int v = r8(r);
        if (i < NSS)
            s->s[i] = (uint8_t)v;
    }
    n = r8(r);
    for (i = 0; i < n; i++) {
        int k = r8(r), d = r8(r), dp = r8(r);
        if ((k >> 4) < NMS && (k & 15) < NMSLOT) {
            s->mdst[k >> 4][k & 15] = (uint8_t)d;
            s->mdep[k >> 4][k & 15] = (int8_t)dp;
        }
    }
    rbytes(r, s->arp_ofs, 16);
    s->arp_on = (uint16_t)r16(r);
    rbytes(r, s->name, NAMELEN);
    s->name[NAMELEN - 1] = 0;
}

int sound_pack(const sound_t *s, uint8_t *out, int max)
{
    wr_t w = {out, 0, max, 0};
    put_sound(&w, s);
    return w.bad ? -1 : w.n;
}
int sound_unpack(sound_t *s, const uint8_t *in, int n)
{
    rd_t r = {in, n, 0, 0};
    get_sound(&r, s);
    return r.bad ? -1 : 0;
}

/* the layout of sound_t: p[nsp] (int16), s[nss], mdst[5][4], mdep[5][4], arp_ofs[16], arp_on (int16, even
 * offset), name[12], the whole even */
int sound_raw_size(int nsp, int nss)
{
    int o = 2 * nsp + nss + 2 * NMS * NMSLOT + 16;
    o = (o + 1) & ~1;
    return (o + 2 + NAMELEN + 1) & ~1;
}
/* a sound read from a bank: its name in the screen's characters (32..95, a..z) and ended, its routing and setup in
 * range; else it becomes an empty slot */
static int sound_check(sound_t *s, int nss, int ok)
{
    int i, j;
    for (i = 0; i < NMS; i++)
        for (j = 0; j < NMSLOT; j++)
            if (s->mdst[i][j] >= NDEST)              /* (the destinations: under NDEST) */
                ok = 0;
    for (i = 0; i < NAMELEN && s->name[i]; i++) {
        uint8_t ch = (uint8_t)s->name[i];
        if (ch < 32 || (ch > 95 && (ch < 'a' || ch > 'z')))
            ok = 0;
    }
    if (i == NAMELEN)
        ok = 0;
    if (s->s[S_PLAYMODE] > 3 || s->s[S_VEL2VOL] > 3 || s->s[S_ARPMODE] >= NARP || s->s[S_MONOPRIO] > 2 ||
        s->s[S_OCT] > 4 || (nss > S_ENGINE && s->s[S_ENGINE] >= NENG))
        ok = 0;
    for (i = S_DM1; i < nss && i <= S_DM8; i++)
        if (s->s[i] >= NDM)
            ok = 0;
    if ((nss > S_ARPCHD && s->s[S_ARPCHD] >= NACH) || (nss > S_ARPINV && s->s[S_ARPINV] > 3))
        ok = 0;
    if ((nss > S_CHVOIC && s->s[S_CHVOIC] >= NCV) || (nss > S_CHBASS && s->s[S_CHBASS] > 1) ||
        (nss > S_CHSDIR && s->s[S_CHSDIR] >= NSD))
        ok = 0;
    if ((nss > S_SCALE && s->s[S_SCALE] >= NKSCALE) || (nss > S_SCROOT && s->s[S_SCROOT] > 11) ||
        (nss > S_SCHD && s->s[S_SCHD] >= NDCH))   /* (0.12) */
        ok = 0;
    if (!ok) {
        sound_init(s);
        s->name[0] = 0;
    }
    return ok;
}

/* the banks' compact form (version 3): each parameter in the bits its range needs (value - lo), then the setup,
 * the routing, the arpeggiator and the name as bytes. Built on the parameters' ranges, which never change */
static int pbits(int p)
{
    int r = PDESC[p].hi - PDESC[p].lo, n = 0;
    while (r >> n)
        n++;
    return n;
}
int sound_bits_size(int nsp, int nss)
{
    int p, bits = 0;
    for (p = 0; p < nsp && p < NSP; p++)
        bits += pbits(p);
    return (bits + 7) / 8 + nss + 2 * NMS * NMSLOT + 16 + 2 + NAMELEN;
}
void sound_to_bits(const sound_t *s, uint8_t *b)
{
    int p, at = 0, n = sound_bits_size(NSP, NSS), i, j, o;
    bytes_zero(b, (uint32_t)n);
    for (p = 0; p < NSP; p++) {
        int nb = pbits(p), v = s->p[p], k;
        v = (v < PDESC[p].lo ? PDESC[p].lo : v > PDESC[p].hi ? PDESC[p].hi : v) - PDESC[p].lo;
        for (k = 0; k < nb; k++, at++)
            if (v >> k & 1)
                b[at >> 3] |= (uint8_t)(1u << (at & 7));
    }
    o = (at + 7) / 8;
    for (i = 0; i < NSS; i++)
        b[o++] = s->s[i];
    for (i = 0; i < NMS; i++)
        for (j = 0; j < NMSLOT; j++) {
            b[o + i * NMSLOT + j] = s->mdst[i][j];
            b[o + NMS * NMSLOT + i * NMSLOT + j] = (uint8_t)s->mdep[i][j];
        }
    o += 2 * NMS * NMSLOT;
    for (i = 0; i < 16; i++)
        b[o++] = (uint8_t)s->arp_ofs[i];
    b[o++] = (uint8_t)s->arp_on;
    b[o++] = (uint8_t)(s->arp_on >> 8);
    for (i = 0; i < NAMELEN; i++)
        b[o++] = (uint8_t)s->name[i];
}
int sound_from_bits(sound_t *s, const uint8_t *b, int nsp, int nss)
{
    int p, at = 0, i, j, o;
    sound_init(s);
    for (p = 0; p < nsp && p < NSP; p++) {
        int nb = pbits(p), v = 0, k;
        for (k = 0; k < nb; k++, at++)
            v |= (b[at >> 3] >> (at & 7) & 1) << k;
        v += PDESC[p].lo;
        if (v <= PDESC[p].hi)
            s->p[p] = (int16_t)v;
    }
    o = (at + 7) / 8;
    for (i = 0; i < nss; i++, o++)
        if (i < NSS)
            s->s[i] = b[o];
    for (i = 0; i < NMS; i++)
        for (j = 0; j < NMSLOT; j++) {
            s->mdst[i][j] = b[o + i * NMSLOT + j];
            s->mdep[i][j] = (int8_t)b[o + NMS * NMSLOT + i * NMSLOT + j];
        }
    o += 2 * NMS * NMSLOT;
    for (i = 0; i < 16; i++)
        s->arp_ofs[i] = (int8_t)b[o++];
    s->arp_on = (uint16_t)(b[o] | b[o + 1] << 8);
    o += 2;
    for (i = 0; i < NAMELEN; i++)
        s->name[i] = (char)b[o++];
    return sound_check(s, nss, 1);
}

int sound_from_raw(sound_t *s, const uint8_t *b, int nsp, int nss)
{
    int i, j, o, ok = 1;
    sound_init(s);
    for (i = 0; i < nsp; i++) {
        int v = (int16_t)(b[2 * i] | b[2 * i + 1] << 8);
        if (i < NSP && v >= PDESC[i].lo && v <= PDESC[i].hi)
            s->p[i] = (int16_t)v;
        else if (i < NSP)
            ok = 0;
    }
    o = 2 * nsp;
    for (i = 0; i < nss; i++)
        if (i < NSS)
            s->s[i] = b[o + i];
    o += nss;
    for (i = 0; i < NMS; i++)
        for (j = 0; j < NMSLOT; j++) {
            s->mdst[i][j] = b[o + i * NMSLOT + j];
            s->mdep[i][j] = (int8_t)b[o + NMS * NMSLOT + i * NMSLOT + j];
        }
    o += 2 * NMS * NMSLOT;
    for (i = 0; i < 16; i++)
        s->arp_ofs[i] = (int8_t)b[o + i];
    o = (o + 16 + 1) & ~1;
    s->arp_on = (uint16_t)(b[o] | b[o + 1] << 8);
    o += 2;
    for (i = 0; i < NAMELEN; i++)
        s->name[i] = (char)b[o + i];
    return sound_check(s, nss, ok);
}

int pattern_pack(const pattern_t *p, uint8_t *out, int max)
{
    wr_t w = {out, 0, max, 0};
    int t, s, i, n;
    w16(&w, (int)(PAT_MAGIC >> 16));
    w16(&w, (int)(PAT_MAGIC & 0xFFFF));
    w8(&w, PACK_VERSION);
    wbytes(&w, p->name, NAMELEN);
    w16(&w, p->bpm10);
    w8(&w, p->swing);
    w8(&w, p->lenmode);
    w8(&w, p->mlen);
    w8(&w, p->chlen);
    w8(&w, p->steal);
    w8(&w, p->reuse);
    w8(&w, NFXP);
    for (i = 0; i < NFXP; i++)
        w16(&w, p->fx[i]);
    for (t = 0; t < NTRK; t++) {
        const track_t *k = &p->trk[t];
        w8(&w, NTP_END - NSP);
        for (i = 0; i < NTP_END - NSP; i++)
            w16(&w, k->tp[i]);
        w8(&w, k->len); w8(&w, k->scale); w8(&w, k->transpose); w8(&w, k->voices); w8(&w, k->unison);
        w8(&w, k->uspread); w8(&w, k->layer); w8(&w, k->kscale); w8(&w, k->kroot); w8(&w, k->kfold);
        w8(&w, k->kchord);
        put_sound(&w, &p->snd[t]);
    }
    for (t = 0; t < NTRK; t++) {
        uint8_t mask[8] = {0};
        for (s = 0; s < NSTEP; s++)
            if (p->trig[t][s].flags & (TF_NOTE | TF_LOCK))
                mask[s >> 3] |= (uint8_t)(1u << (s & 7));
        wbytes(&w, mask, 8);
        for (s = 0; s < NSTEP; s++) {
            const trig_t *g = &p->trig[t][s];
            if (!(g->flags & (TF_NOTE | TF_LOCK)))
                continue;
            w8(&w, g->flags & (TF_NOTE | TF_LOCK));
            w8(&w, g->micro);
            w8(&w, g->cond);
            w8(&w, g->nnote);
            w8(&w, g->note);
            w8(&w, g->vel);
            w8(&w, g->len);
            for (i = 1; i < g->nnote && i < NNOTE; i++)
                w8(&w, g->chord[i - 1]);
        }
    }
    n = 0;
    for (i = 0; i < p->nlock; i++)
        n += p->lock[i].f.param != P_NONE;
    w16(&w, n);
    for (i = 0; i < p->nlock; i++)
        if (p->lock[i].f.param != P_NONE) {
            w8(&w, p->lock[i].f.step);
            w8(&w, p->lock[i].f.param);
            w16(&w, p->lock[i].f.val);
        }
    return w.bad ? -1 : w.n;
}

int pattern_unpack(pattern_t *p, const uint8_t *in, int n)
{
    rd_t r = {in, n, 0, 0};
    int t, s, i, k, cnt, ver;
    uint32_t magic;
    magic = (uint32_t)(uint16_t)r16(&r) << 16;
    magic |= (uint16_t)r16(&r);
    ver = r8(&r);
    if (magic != PAT_MAGIC || ver < 1 || ver > PACK_VERSION)
        return -1;
    pattern_init(p);
    rbytes(&r, p->name, NAMELEN);
    p->name[NAMELEN - 1] = 0;
    p->bpm10 = (uint16_t)r16(&r);
    p->swing = (uint8_t)r8(&r);
    p->lenmode = (uint8_t)r8(&r);
    p->mlen = (uint8_t)r8(&r);
    p->chlen = (uint8_t)r8(&r);
    p->steal = (uint8_t)r8(&r);
    p->reuse = (uint8_t)r8(&r);
    cnt = r8(&r);
    for (i = 0; i < cnt; i++) {
        int v = r16(&r);
        if (i < NFXP)
            p->fx[i] = (int16_t)v;
    }
    for (t = 0; t < NTRK; t++) {
        track_t *tk = &p->trk[t];
        cnt = r8(&r);
        for (i = 0; i < cnt; i++) {
            int v = r16(&r);
            if (i < NTP_END - NSP)
                tk->tp[i] = (int16_t)v;
        }
        tk->len = (uint8_t)r8(&r); tk->scale = (uint8_t)r8(&r); tk->transpose = (int8_t)r8(&r);
        tk->voices = (uint8_t)r8(&r); tk->unison = (uint8_t)r8(&r); tk->uspread = (uint8_t)r8(&r);
        tk->layer = (uint8_t)r8(&r); tk->kscale = (uint8_t)r8(&r); tk->kroot = (uint8_t)r8(&r);
        tk->kfold = (uint8_t)r8(&r); tk->kchord = (uint8_t)r8(&r);
        if (tk->len < 2 || tk->len > NSTEP) tk->len = 16;
        if (tk->scale > 6) tk->scale = 4;
        if (tk->voices > NVOICE) tk->voices = 0;
        if (tk->unison < 1 || tk->unison > 4) tk->unison = 1;
        get_sound(&r, &p->snd[t]);
    }
    for (t = 0; t < NTRK; t++) {
        uint8_t mask[8];
        rbytes(&r, mask, 8);
        for (s = 0; s < NSTEP; s++) {
            trig_t *g = &p->trig[t][s];
            if (!(mask[s >> 3] >> (s & 7) & 1u))
                continue;
            g->flags = (uint8_t)(r8(&r) & (TF_NOTE | TF_LOCK));
            g->micro = (int8_t)r8(&r);
            g->cond = (uint8_t)r8(&r);
            g->nnote = (uint8_t)r8(&r);
            g->note = (uint8_t)r8(&r);
            g->vel = (uint8_t)r8(&r);
            g->len = (uint8_t)r8(&r);
            if (g->nnote < 1 || g->nnote > NNOTE) g->nnote = 1;
            for (k = 1; k < g->nnote; k++)
                g->chord[k - 1] = (int8_t)r8(&r);
            if (g->micro < -23 || g->micro > 23) g->micro = 0;
            if (g->cond >= C_END) g->cond = 0;
        }
    }
    cnt = (uint16_t)r16(&r);
    for (i = 0; i < cnt && i < NLOCK; i++) {
        plock_t l;
        l.f.step = (uint8_t)r8(&r);
        l.f.param = (uint8_t)r8(&r);
        l.f.val = (int16_t)r16(&r);
        if (ver == 1 && l.f.param >= P_SRR && l.f.param < P_SOUND)   /* version 1: no LO-FI, no retrig */
            l.f.param = (uint8_t)(l.f.param + 2 + (l.f.param + 2 >= P_RTRG - NDRP - NSP12 ? 3 : 0));   /* (in version 2's ids) */
        if (ver <= 2 && l.f.param >= P_D5TUN && l.f.param < P_SOUND)   /* version 2: no drum pads 5-8 */
            l.f.param = (uint8_t)(l.f.param + NDRP);
        if (ver <= 3 && l.f.param >= P_DA1 && l.f.param < P_SOUND)     /* version 3: none of 0.12's sound parameters */
            l.f.param = (uint8_t)(l.f.param + NSP12);
        if (l.f.param >= NPARAM && l.f.param != P_SOUND)
            continue;
        p->lock[p->nlock++].w = l.w;
    }
    for (t = 0; t < NTRK; t++)
        for (s = 0; s < NSTEP; s++)
            trig_flags_update(p, t, s);
    return r.bad ? -1 : 0;
}

/* [MANUAL 11.2.1: ROOT C0-G10] Elektron's names: MIDI 0 is C0, 60 is C5 */
void note_name(int note, char *out)
{
    static const char N[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int o = note / 12, i = 0;
    const char *n = N[note % 12];
    while (*n)
        out[i++] = *n++;
    if (o >= 10) {
        out[i++] = '1';
        o -= 10;
    }
    out[i++] = (char)('0' + o);
    out[i] = 0;
}
