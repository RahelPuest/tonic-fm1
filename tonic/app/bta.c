/* SPDX-License-Identifier: MPL-2.0
 * TONIC (0.32): Bluetooth audio, the FM-1's sound to headphones and speakers (A2DP, SBC) on fm1-base's own stack
 * (bt_audio = true: plat_bta_*). The MENU's BT AUDIO row (on / off now) and, OCT+ on it, its page: AUDIO, AUTOCONNECT,
 * OUTPUT, the four remembered devices (the last one first), + ADD HEADPHONES (a scan of about 10 s, then the list).
 * The base remembers the devices and their keys in the Bluetooth record (APP_BLE_STORE, in TONIC's settings). While
 * it streams, the FM-1's own output is muted. Part of app.c's unity build. */

enum { BA_ON, BA_AUTO, BA_OUT, BA_DEV };             /* the page's rows; the remembered devices from BA_DEV */
static struct {
    uint8_t on;                                      /* AUDIO ON (plat_bta_on) */
    uint8_t row, adding, add_i, forget;              /* the page; adding 1: the scan, 2: its list; forget: 1 + i */
    uint8_t st_was, boot_done;
    char err[28];
} hp;

static void hp_init(void) { memset(&hp, 0, sizeof hp); }   /* (app_init: every start) */
static int hp_nknown(void)
{
    int n = 0;
    while (n < 4 && plat_bta_known(n, 0))
        n++;
    return n;
}
static int hp_nfound(void)
{
    int n = 0;
    while (n < 16 && plat_bta_found(n, 0))
        n++;
    return n;
}
static int hp_linked(int st) { return st == PLAT_BTA_LINKED || st == PLAT_BTA_SETUP || st == PLAT_BTA_STREAMING; }
static int hp_on_now(void) { return hp.on; }
static void hp_set_on(int on)
{
    hp.on = (uint8_t)(on != 0);
    plat_bta_on(hp.on);
    hp.adding = 0;
    hp.forget = 0;
    tx_cpy(hp.err, "");
}

/* every frame (the main loop): AUTOCONNECT at the start, a lost link said */
static void hp_poll(void)
{
    int st = plat_bta_state(), lost = plat_bta_lost();
    char nm[18];
    if (!hp.boot_done && plat_ms() > 1500u) {        /* (after the start's own radio work) */
        hp.boot_done = 1;
        if (!set.bta_noauto && set.ble_ok && set.ble.bta_n) {   /* (the base reads its list when the radio starts) */
            hp_set_on(1);
            (void)plat_bta_reconnect(0);
        }
    }
    if (st != hp.st_was) {
        if (st == PLAT_BTA_STREAMING && plat_bta_known(0, nm))
            app_say("BT AUDIO", nm);
        else if (st == PLAT_BTA_REFUSED)
            tx_cpy(hp.err, plat_bta_timed_out() ? "NO ANSWER" : "REFUSED: PAIRING MODE?");
        else if (st == PLAT_BTA_FAILED)              /* (paged, no answer; or the link closed) */
            tx_cpy(hp.err, "NO ANSWER: HEADPHONES ON?");
        else if (st == PLAT_BTA_NO_LAST)
            tx_cpy(hp.err, "NOT AROUND");
        hp.st_was = (uint8_t)st;
        ui.redraw = 1;
    }
    if (lost != PLAT_BTA_LOST_NONE) {
        app_say("BT AUDIO LOST", lost == PLAT_BTA_LOST_PAIRING || lost == PLAT_BTA_LOST_ENCRYPTION ? "PAIRING" : "");
        tx_cpy(hp.err, "LOST");
    }
    if (hp.adding == 1 && st != PLAT_BTA_SCANNING) {   /* the scan over: its list */
        hp.adding = 2;
        hp.add_i = 0;
        ui.redraw = 1;
    }
    if (ui.ov == OV_BTA && (hp.adding == 1 || st == PLAT_BTA_PAGING || st == PLAT_BTA_SETUP || st == PLAT_BTA_LINKED))
        ui.redraw = 1;                               /* (the scan's count, the set-up's progress) */
}

/* the page: MENU > BT AUDIO, OCT+ */
static int hp_nrows(void) { return BA_DEV + hp_nknown() + 1; }
static void hp_state_str(char *s)
{
    int st = plat_bta_state();
    char nm[18];
    if (!hp.on || st == PLAT_BTA_OFF) {
        tx_cpy(s, "OFF");
        return;
    }
    switch (st) {
    case PLAT_BTA_SCANNING: tx_cpy(s, "SEARCHING"); return;
    case PLAT_BTA_PAGING: tx_cpy(s, "CONNECTING"); return;
    case PLAT_BTA_LINKED: case PLAT_BTA_SETUP: tx_cpy(s, "SETTING UP"); return;
    case PLAT_BTA_STREAMING:
        tx_cpy(s, plat_bta_out_device() ? "PLAYING: " : "HELD (FM-1 OUT): ");
        if (plat_bta_known(0, nm))
            tx_cat(s, nm);
        return;
    }
    tx_cpy(s, hp_nknown() ? "ON, NOT CONNECTED" : "ON, NOTHING PAIRED");
}
static void hp_row_str(int r, char *name, char *val)
{
    int st = plat_bta_state();
    char nm[18];
    name[0] = val[0] = 0;
    switch (r) {
    case BA_ON: tx_cpy(name, "AUDIO"); tx_cpy(val, hp.on ? "ON" : "OFF"); return;
    case BA_AUTO: tx_cpy(name, "AUTOCONNECT"); tx_cpy(val, set.bta_noauto ? "OFF" : "ON"); return;
    case BA_OUT: tx_cpy(name, "OUTPUT"); tx_cpy(val, plat_bta_out_device() ? "BLUETOOTH" : "FM-1"); return;
    }
    r -= BA_DEV;
    if (plat_bta_known(r, nm)) {
        int on = r == 0 && hp.on && hp_linked(st);
        tx_cpy(name, on ? "* " : "  ");
        tx_cat(name, nm[0] ? nm : "?");
        tx_cpy(val, on ? (st == PLAT_BTA_STREAMING ? "PLAYING" : "...") : "");
    } else
        tx_cpy(name, "+ ADD HEADPHONES");
}
static void hp_line(int y, int sel, const char *a, const char *b)
{
    if (sel) {
        o_rect(0, y - 1, OW, 7, 1);
        o_ts(2, y, a, 0);
        o_ts(OW - o_ws(b) - 2, y, b, 0);
    } else {
        o_ts(2, y, a, 1);
        o_ts(OW - o_ws(b) - 2, y, b, 1);
    }
}
static void hp_draw(void)
{
    char a[32], b[24];
    int i, n;
    if (hp.adding) {                                 /* + ADD's scan and list */
        o_header("ADD HEADPHONES", "");
        o_ink = K_DIM;
        n = hp_nfound();
        o_ts(0, 12, hp.adding == 1 ? "SEARCHING (PAIRING MODE ON)" : n ? "OCT+: PAIR AND PLAY" : "NOTHING FOUND: OCT+ AGAIN", 1);
        o_ink = K_FG;
        for (i = 0; i < n && i < 11; i++) {
            char nm[18];
            plat_bta_found(i, nm);
            hp_line(21 + i * 7, hp.adding == 2 && i == hp.add_i, nm[0] ? nm : "(NO NAME)", "");
        }
        o_ink = K_DIM;
        o_ts(0, 115, "SELECT  OCT+ PAIR  OCT- BACK", 1);
        o_ink = K_FG;
        return;
    }
    o_header("BT AUDIO", "");
    o_ink = K_ACC;
    hp_state_str(a);
    o_ts(0, 12, a, 1);
    o_ink = K_FG;
    n = hp_nrows();
    for (i = 0; i < n; i++) {
        hp_row_str(i, a, b);
        hp_line(21 + i * 7, i == hp.row, a, b);
    }
    o_ink = K_DIM;
    if (plat_bta_state() == PLAT_BTA_STREAMING && plat_bta_latency_us() >= 0) {
        tx_cpy(a, "DELAY ");
        tx_num(a + tx_len(a), (int)(plat_bta_latency_us() / 1000));
        tx_cat(a, " MS + HEADPHONES");
        o_ts(0, 101, a, 1);
    }
    if (hp.forget) {
        char nm[18];
        plat_bta_known(hp.forget - 1, nm);
        tx_cpy(a, "FORGET ");
        tx_cat(a, nm);
        tx_cat(a, "? OCT+");
        o_ink = K_ACC;
        o_ts(0, 108, a, 1);
        o_ink = K_DIM;
    } else if (hp.err[0])
        o_ts(0, 108, hp.err, 1);
    o_ts(0, 115, hp.row >= BA_DEV ? "OCT+ CONNECT  K1 LEFT FORGET" : "SELECT ROW  K1 VALUE  OCT-", 1);
    o_ink = K_FG;
}
static void hp_enc(int e, int d)                    /* the page's knobs */
{
    ui.redraw = 1;
    if (hp.adding) {
        if (e == EN_SELECT && hp.adding == 2 && hp_nfound())
            hp.add_i = (uint8_t)clamp(hp.add_i + d, 0, hp_nfound() - 1);
        return;
    }
    if (e == EN_SELECT) {
        hp.row = (uint8_t)clamp(hp.row + d, 0, hp_nrows() - 1);
        hp.forget = 0;
    } else if (e == EN_K1) {
        switch (hp.row) {
        case BA_ON: if ((d > 0) != hp.on) hp_set_on(d > 0); break;
        case BA_AUTO: set.bta_noauto = (uint8_t)(d < 0); set_dirty = 1; break;
        case BA_OUT: plat_bta_output(d > 0 ? 0 : 1); break;   /* (right: the FM-1's own, left: Bluetooth) */
        default:
            if (hp.row - BA_DEV < hp_nknown() && d < 0)
                hp.forget = (uint8_t)(hp.row - BA_DEV + 1);
            break;
        }
    }
}
static void hp_oct(int d)                           /* OCT+ acts, OCT- goes back */
{
    int st = plat_bta_state();
    ui.redraw = 1;
    if (hp.adding) {
        if (d < 0) {
            hp.adding = 0;
            plat_bta_scan(0);
        } else if (hp.adding == 2 && !hp_nfound()) { /* NOTHING FOUND: again */
            hp.adding = 1;
            plat_bta_scan(1);
        } else if (hp.adding == 2) {                 /* pair the pick and play to it */
            plat_bta_pick(hp.add_i);
            hp.adding = 0;
            hp.row = BA_DEV;
            tx_cpy(hp.err, "");
        }
        return;
    }
    if (d < 0) {                                     /* back to the MENU */
        if (hp.forget)
            hp.forget = 0;
        else
            ui.ov = OV_MENU;
        return;
    }
    if (hp.forget) {                                 /* FORGET: yes */
        int i = hp.forget - 1;
        char nm[18];
        if (i == 0 && hp_linked(st))
            plat_bta_hangup();
        plat_bta_known(i, nm);
        plat_bta_forget(i);
        app_say("FORGOTTEN", nm);
        hp.forget = 0;
        if (hp.row >= hp_nrows())
            hp.row = (uint8_t)(hp_nrows() - 1);
        return;
    }
    if (hp.row == BA_ON) {
        hp_set_on(!hp.on);
        return;
    }
    if (hp.row == BA_OUT) {
        plat_bta_output(!plat_bta_out_device());
        return;
    }
    if (hp.row < BA_DEV)
        return;
    if (!hp.on)
        hp_set_on(1);
    tx_cpy(hp.err, "");
    if (hp.row - BA_DEV < hp_nknown()) {             /* connect it now, or hang up the one playing */
        int i = hp.row - BA_DEV;
        if (i == 0 && hp_linked(st))
            plat_bta_hangup();
        else {
            if (hp_linked(st) || st == PLAT_BTA_PAGING)
                plat_bta_hangup();
            if (plat_bta_reconnect(i) != 0)
                tx_cpy(hp.err, "BUSY: TRY AGAIN");
            hp.row = BA_DEV;
        }
    } else {                                         /* + ADD HEADPHONES */
        if (hp_linked(st) || st == PLAT_BTA_PAGING)
            plat_bta_hangup();
        hp.adding = 1;
        plat_bta_scan(1);
    }
}
