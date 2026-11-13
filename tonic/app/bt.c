/* SPDX-License-Identifier: MPL-2.0
 * TONIC (0.28): Bluetooth as Hortator's Bluetooth version has it, after its manual (its ideas; no code: it is GPL):
 * the MENU's BLUETOOTH page (BLUETOOTH, AUTOSTART, ROLE, MODE, RECONNECT, the saved devices, + ADD A DEVICE), saved
 * devices that reconnect by themselves, the M-VAVE SMC-Mixer in its DAW (Mackie Control) mode as the mixer, and the
 * clock's source and outputs chosen (GLO CLK, SYNC; midi.c). On fm1-base's own stack (ble_stack = "own", ble_central
 * = 3: no pairing, APP_BLE_PEERS: the devices kept here): one link at a time, a computer's (ROLE DEVICE) or a
 * device's (ROLE HOST). Part of app.c's unity build. */

#define BT_SCAN_MS 8000u
#define BT_SEEN_MS 3000u                             /* a saved device heard this lately is NEAR */
enum { BTK_CTRL, BTK_FM1, BTK_SMC };
enum { BR_ON, BR_AUTO, BR_ROLE, BR_MODE, BR_RECON, BR_DEV };   /* the page's rows; the devices from BR_DEV */
static const uint8_t MIDI_UUID[16] = {0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
                                      0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03};   /* (air order) */
typedef struct { uint8_t addr[6], addr_rand, midi; char name[14]; uint32_t t; } bt_found_t;
static struct {
    uint8_t on;                                      /* Bluetooth runs (BLUETOOTH ON) */
    uint8_t row, top, adding, add_i, forget;         /* the page; adding 1: + ADD's scan, 2: its list; forget: 1 + i */
    uint32_t add_t;
    int8_t cur;                                      /* the saved device asked or linked, -1 none */
    uint8_t dev_was, host_was, scanning;
    uint32_t retry_t, retry_ms;
    uint32_t seen_t[8];
    bt_found_t f[8];
    uint8_t nf;
    char err[28];
    uint8_t smc;                                     /* the linked device is the SMC-Mixer (DAW mode) */
    uint8_t picked;                                  /* SMC: the faders that have reached their track's level */
    int8_t fad[NTRK];                                /* SMC: each fader's last value (-1 none yet) */
    uint32_t led_sent, led_t;
    int8_t vk[4];                                    /* SMC: knob 1-4 turns for the page's knobs (ui.c) */
} bt = {.cur = -1};
volatile uint32_t bt_to = TO_BLE;                    /* where MIDI goes on Bluetooth (no MIDI to the SMC-Mixer) */
volatile uint8_t bt_smc_in;                          /* the device's MIDI is the SMC-Mixer's (app_tick: to bt_poll) */
#define SMC_Q 64
static uint32_t smc_q[SMC_Q];
static volatile uint32_t smc_w, smc_r;

static void bt_upper(char *d, const uint8_t *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && i < 13u; i++) {
        char c = (char)s[i];
        d[i] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c < 32 || c > 126 ? '?' : c;
    }
    d[i] = 0;
}
static int bt_kind(const char *name)
{
    if (name[0] == 'S' && name[1] == 'M' && name[2] == 'C')
        return BTK_SMC;
    if ((name[0] == 'T' && name[1] == 'O' && name[2] == 'N') || (name[0] == 'F' && name[1] == 'M' && name[2] == '-') ||
        (name[0] == 'F' && name[1] == 'E' && name[2] == 'L'))
        return BTK_FM1;
    return BTK_CTRL;
}
/* every advertising report of a scan (fm1-base, main loop): the saved devices heard, + ADD's list */
void app_ble_scan_report(const uint8_t *pdu, uint32_t n, uint32_t rssi)
{
    uint32_t type, len, i, k;
    const uint8_t *d;
    char name[14] = "";
    int midi = 0;
    (void)rssi;
    if (n < 8u)
        return;
    type = pdu[0] & 15u;
    len = pdu[1];
    if (len + 2u > n)
        len = n - 2u;
    if (len < 6u || (type != 0u && type != 2u && type != 4u && type != 6u))   /* ADV_IND, NONCONN, SCAN_RSP, SCAN_IND */
        return;
    d = pdu + 8;
    len -= 6u;
    for (i = 0; i + 1u < len;) {                     /* its AD structures: the name, the MIDI service */
        uint32_t l = d[i], t;
        if (!l || i + 1u + l > len)
            break;
        t = d[i + 1];
        if ((t == 0x08u || t == 0x09u) && !name[0])
            bt_upper(name, d + i + 2, l - 1u);
        if (t == 0x06u || t == 0x07u)
            for (k = 0; k + 16u <= l - 1u; k += 16u)
                if (!memcmp(d + i + 2 + k, MIDI_UUID, 16))
                    midi = 1;
        i += l + 1u;
    }
    for (k = 0; k < set.bt_ndev; k++)
        if (!memcmp(set.bt_dev[k].addr, pdu + 2, 6))
            bt.seen_t[k] = plat_ms() | 1u;
    if (bt.adding) {
        for (k = 0; k < bt.nf && memcmp(bt.f[k].addr, pdu + 2, 6); k++)
            ;
        if (k == bt.nf) {
            for (i = 0; i < set.bt_ndev; i++)        /* (a saved one is not offered again) */
                if (!memcmp(set.bt_dev[i].addr, pdu + 2, 6))
                    return;
            if (bt.nf >= 8u)
                return;
            memset(&bt.f[k], 0, sizeof bt.f[k]);
            memcpy(bt.f[k].addr, pdu + 2, 6);
            bt.f[k].addr_rand = (uint8_t)(pdu[0] >> 6 & 1u);
            bt.nf++;
        }
        if (name[0])
            tx_cpy(bt.f[k].name, name);
        bt.f[k].midi |= (uint8_t)midi;
        bt.f[k].t = plat_ms();
        ui.redraw = 1;
    }
}
void app_ble_device_keys(const struct plat_ble_keys *k) { (void)k; }   /* (no pairing: ble_central = 3) */

static const char *bt_kind_name(int k) { return k == BTK_SMC ? "SMC" : k == BTK_FM1 ? "FM-1" : "CTRL"; }
static int bt_is_smc(int i)
{
    return i >= 0 && (set.bt_mode == 2 || (set.bt_mode == 0 && set.bt_dev[i].kind == BTK_SMC));
}
static int bt_connect_saved(int i)
{
    struct plat_ble_peer p;
    memset(&p, 0, sizeof p);
    memcpy(p.addr, set.bt_dev[i].addr, 6);
    p.addr_rand = set.bt_dev[i].addr_rand;
    if (plat_ble_connect_peer(&p) != 0)
        return -1;
    bt.cur = (int8_t)i;
    bt.scanning = 0;
    return 0;
}
static void bt_to_front(int i)                       /* (ADD, OCT+ "use now": tried first from now on) */
{
    struct bt_dev d = set.bt_dev[i];
    for (; i > 0; i--)
        set.bt_dev[i] = set.bt_dev[i - 1];
    set.bt_dev[0] = d;
    set_dirty = 1;
}
static void bt_scan(int on)
{
    if (on != bt.scanning) {
        plat_ble_scan(on);
        bt.scanning = (uint8_t)on;
    }
}
static void bt_set_on(int on)
{
    bt.on = (uint8_t)(on != 0);
    plat_ble_enable(bt.on);
    bt.scanning = 0;
    bt.adding = 0;
    bt.cur = -1;
    bt.retry_t = 0;
    tx_cpy(bt.err, "");
}
static void bt_init(void)
{
    bt.on = (uint8_t)!set.bt_off;
    bt.retry_ms = 2000;
}

/* SMC-Mixer (Mackie Control, DAW mode): its presses, faders and knobs on TONIC's four tracks; its LEDs from TONIC */
static void smc_led(uint32_t note, int on)
{
    plat_midi_send(0x09u | 0x90u << 8 | note << 16 | (on ? 0x7Fu : 0u) << 24, TO_BLE_DEV);
}
static void smc_leds(int all)
{
    uint32_t want = 0, diff, i;
    static const uint8_t N[11] = {0x5E, 0x5D, 0x5F, 0x10, 0x11, 0x12, 0x13, 0x08, 0x09, 0x0A, 0x0B};
    if (plat_midi_out_room(TO_BLE_DEV) < 12u)
        return;
    want |= eng.playing ? 1u : 2u;
    want |= eng.rec_live || ui.grid ? 4u : 0u;
    want |= (uint32_t)(eng.mute & 15u) << 3;
    want |= (uint32_t)(eng.solo & 15u) << 7;
    want |= 1u << (11 + ui.trk);                     /* SELECT 1-4 (0x18..) */
    diff = all ? 0xFFFFFFFFu : want ^ bt.led_sent;
    for (i = 0; i < 11u; i++)
        if (diff >> i & 1u)
            smc_led(N[i], (int)(want >> i & 1u));
    for (i = 0; i < 4u; i++)
        if (diff >> (11 + i) & 1u)
            smc_led(0x18u + i, (int)(want >> (11 + i) & 1u));
    bt.led_sent = want;
}
static void smc_in(uint32_t pkt)
{
    uint32_t st = pkt >> 8 & 0xFFu, d1 = pkt >> 16 & 0x7Fu, d2 = pkt >> 24 & 0x7Fu, t = st & 15u;
    if ((st & 0xF0u) == 0xE0u && t < NTRK) {         /* a fader: the track's LEVEL, once picked up */
        int v = (int)((d2 << 7 | d1) >> 7), cur = pcur->trk[t].tp[P_TLVL - NSP];
        if (!(bt.picked >> t & 1u)) {                /* within 2 of it, or passing it */
            int was = bt.fad[t];
            bt.fad[t] = (int8_t)v;
            if ((v - cur > 2 || cur - v > 2) && (was < 0 || (was - cur) * (v - cur) > 0))
                return;
            bt.picked |= (uint8_t)(1u << t);
        }
        pcur->trk[t].tp[P_TLVL - NSP] = (int16_t)v;
        mark_dirty();
        ui.redraw = 1;
    } else if ((st & 0xF0u) == 0xB0u && d1 >= 0x10u && d1 < 0x14u) {   /* V-Pot 1-4: the page's knobs 1-4 */
        int d = (int)(d2 & 0x3Fu);
        bt.vk[d1 - 0x10u] = (int8_t)clamp(bt.vk[d1 - 0x10u] + (d2 & 0x40u ? -d : d), -64, 64);
    } else if ((st & 0xF0u) == 0x90u && d2) {        /* the buttons: their presses */
        if (d1 >= 0x10u && d1 < 0x14u)
            eng.mute ^= (uint8_t)(1u << (d1 - 0x10u));
        else if (d1 >= 0x08u && d1 < 0x0Cu)
            eng.solo ^= (uint8_t)(1u << (d1 - 0x08u));
        else if (d1 >= 0x18u && d1 < 0x1Cu)
            ui.trk = (uint8_t)(d1 - 0x18u);
        else if (d1 == 0x2Eu || d1 == 0x30u)         /* BANK / CHANNEL left */
            ui.trk = (uint8_t)(ui.trk ? ui.trk - 1 : 0);
        else if (d1 == 0x2Fu || d1 == 0x31u)
            ui.trk = (uint8_t)(ui.trk < NTRK - 1 ? ui.trk + 1 : NTRK - 1);
        else if (d1 == 0x5Eu && !eng.playing) {
            song_start();
            engine_play();
        } else if (d1 == 0x5Du && eng.playing)
            engine_stop();
        else if (d1 == 0x5Fu) {
            ui.grid ^= 1u;
            ui.steprec = 0;
        }
        ui.redraw = 1;
    }
}
static int bt_on_now(void) { return bt.on; }
/* (0.31) the USB port as a host (fm1-base usb_host): another FM-1 (or a USB-MIDI device) on one USB-C cable, its MIDI
 * on PORT_USB and TO_USB as a computer's would be; no computer meanwhile. Not saved: every boot is a device */
static uint8_t usbh_on;
static int usbh_on_now(void) { return usbh_on; }
static void usbh_set_on(int on)
{
    usbh_on = (uint8_t)(on != 0);
    plat_usb_host(usbh_on);
}
static void usbh_poll(void)
{
    int m = plat_usb_host_msg(), t;
    if (m == USBH_MSG_ON || m == USBH_MSG_OFF)
        app_say("USB HOST", m == USBH_MSG_ON ? "LOOKING" : "OFF");
    else if (m == USBH_MSG_MIDI)
        app_say("USB CONNECTED", plat_usb_host_name());
    else if (m == USBH_MSG_NOMIDI || m == USBH_MSG_LOW)
        app_say("USB HOST", "NO MIDI DEVICE");
    else if (m == USBH_MSG_LOST && usbh_on)
        app_say("USB LOST", "LOOKING");
    if (plat_usb_lost())                             /* the notes the USB session held */
        for (t = 0; t < NTRK; t++)
            engine_all_off(t);
}
static void bt_smc_push(uint32_t pkt)                /* app_tick: the SMC-Mixer's MIDI, for bt_poll */
{
    if (smc_w - smc_r < SMC_Q) {
        smc_q[smc_w % SMC_Q] = pkt;
        __asm__ volatile("" ::: "memory");
        smc_w++;
    }
}
static int bt_knob_take(int e)                      /* ui.c: an SMC knob's turns as the FM-1's knob e's */
{
    int d;
    if (e < EN_K1 || e > EN_K4)
        return 0;
    d = bt.vk[e - EN_K1];
    bt.vk[e - EN_K1] = 0;
    return d;
}

/* every frame (the main loop): the links, the saved devices reconnected, what happened said, the SMC-Mixer */
static void bt_poll(void)
{
    uint32_t now = plat_ms();
    usbh_poll();
    int ready = bt.on && plat_ble_state() == BLE_READY, dev = ready ? plat_ble_device() : BLE_DEV_OFF;
    int host = ready && plat_ble_connected(), smc;
    if (host != bt.host_was) {                       /* a computer (ROLE DEVICE) */
        app_say(host ? "BT CONNECTED" : "BT LOST", "COMPUTER");
        bt.host_was = (uint8_t)host;
    }
    if ((dev == BLE_DEV_READY) != (bt.dev_was == BLE_DEV_READY)) {
        if (dev == BLE_DEV_READY) {
            app_say("BT CONNECTED", bt.cur >= 0 ? set.bt_dev[bt.cur].name : "");
            bt.retry_ms = 0;
            bt.picked = 0;
            memset(bt.fad, -1, sizeof bt.fad);
            bt.led_sent = 0;
            tx_cpy(bt.err, "");
        } else {
            app_say("BT LOST", bt.cur >= 0 ? set.bt_dev[bt.cur].name : "");
            tx_cpy(bt.err, "LOST ");
            if (bt.cur >= 0)
                tx_cat(bt.err, set.bt_dev[bt.cur].name);
        }
        ui.redraw = 1;
    }
    if (bt.dev_was == BLE_DEV_CONNECTING && dev == BLE_DEV_OFF) {   /* it never answered */
        tx_cpy(bt.err, "NO ANSWER ");
        if (bt.cur >= 0)
            tx_cat(bt.err, set.bt_dev[bt.cur].name);
        ui.redraw = 1;
    }
    if (dev == BLE_DEV_OFF && bt.dev_was != BLE_DEV_OFF)
        bt.scanning = 0;                             /* (a link's end leaves the scan off) */
    bt.dev_was = (uint8_t)dev;
    smc = dev == BLE_DEV_READY && bt_is_smc(bt.cur);
    bt.smc = (uint8_t)smc;
    bt_smc_in = (uint8_t)smc;
    bt_to = smc ? TO_BLE_HOST : TO_BLE;
    while (smc_r != smc_w) {
        uint32_t p = smc_q[smc_r % SMC_Q];
        __asm__ volatile("" ::: "memory");
        smc_r++;
        if (smc)
            smc_in(p);
    }
    if (smc && now - bt.led_t > 40u) {
        bt.led_t = now;
        smc_leds(bt.led_sent == 0);
    }
    if (!ready)
        return;
    if (set.bt_role == 0) {                          /* ROLE DEVICE: no device of ours, no scan */
        if (dev == BLE_DEV_READY || dev == BLE_DEV_CONNECTING)
            plat_ble_disconnect();
        bt_scan(0);
        return;
    }
    if (bt.adding) {                                 /* + ADD: an 8 s scan, then its list */
        if (dev == BLE_DEV_READY || dev == BLE_DEV_CONNECTING)
            plat_ble_disconnect();
        bt_scan(1);
        if (bt.adding == 1 && now - bt.add_t > BT_SCAN_MS) {
            bt.adding = 2;
            ui.redraw = 1;
        }
        return;
    }
    if (dev != BLE_DEV_OFF && dev != BLE_DEV_SCANNING)
        return;                                      /* (linked, or on its way) */
    if (!set.bt_ndev) {
        bt_scan(0);
        return;
    }
    bt_scan(1);                                      /* the saved devices looked for, the first one near connected */
    if ((int32_t)(now - bt.retry_t) >= 0) {
        int i, slow = set.bt_recon == 2 || (set.bt_recon == 0 && eng.playing);
        for (i = 0; i < set.bt_ndev; i++)
            if (bt.seen_t[i] && now - bt.seen_t[i] < BT_SEEN_MS)
                break;
        if (i < set.bt_ndev) {
            bt_scan(0);
            if (bt_connect_saved(i) != 0)
                bt_scan(1);
            bt.retry_ms = !bt.retry_ms ? (slow ? 8000u : 2000u) : bt.retry_ms * 2u > 16000u ? 16000u : bt.retry_ms * 2u;
            bt.retry_t = now + bt.retry_ms;
        }
    }
}

/* the page: MENU > BLUETOOTH, OCT+ */
static int bt_nrows(void) { return BR_DEV + set.bt_ndev + (set.bt_role == 1); }
static void bt_row_str(int r, char *name, char *val)
{
    static const char *const ROLE[2] = {"DEVICE", "HOST"}, *const MODE[3] = {"AUTO", "MIDI", "SMC DAW"},
                             *const REC[3] = {"AUTO", "FAST", "SLOW"};
    name[0] = val[0] = 0;
    switch (r) {
    case BR_ON: tx_cpy(name, "BLUETOOTH"); tx_cpy(val, bt.on ? "ON" : "OFF"); return;
    case BR_AUTO: tx_cpy(name, "AUTOSTART"); tx_cpy(val, set.bt_off ? "OFF" : "ON"); return;
    case BR_ROLE: tx_cpy(name, "ROLE"); tx_cpy(val, ROLE[set.bt_role & 1]); return;
    case BR_MODE: tx_cpy(name, "MODE"); tx_cpy(val, MODE[set.bt_mode % 3]); return;
    case BR_RECON: tx_cpy(name, "RECONNECT"); tx_cpy(val, REC[set.bt_recon % 3]); return;
    }
    r -= BR_DEV;
    if (r < set.bt_ndev) {
        int linked = bt.cur == r && plat_ble_device() == BLE_DEV_READY;
        uint32_t s = bt.seen_t[r];
        tx_cpy(name, linked ? "* " : "  ");
        tx_cat(name, set.bt_dev[r].name[0] ? set.bt_dev[r].name : "?");
        tx_cpy(val, bt_kind_name(set.bt_dev[r].kind));
        tx_cat(val, linked ? " ON" : s && plat_ms() - s < BT_SEEN_MS ? " NEAR" : " AWAY");
    } else
        tx_cpy(name, "+ ADD A DEVICE");
}
static void bt_state_str(char *s)
{
    int st = plat_ble_state(), dev = plat_ble_device();
    if (!bt.on || st == BLE_OFF)
        tx_cpy(s, "OFF");
    else if (st == BLE_FAILED)
        tx_cpy(s, "FAILED: SWITCH OFF AND ON");
    else if (st != BLE_READY)
        tx_cpy(s, "STARTING");
    else if (set.bt_role == 0)
        tx_cpy(s, plat_ble_connected() ? "CONNECTED: A COMPUTER" : "WAITING FOR A HOST: TONIC");
    else if (dev == BLE_DEV_READY) {
        tx_cpy(s, bt.smc ? "SMC DAW: " : "CONNECTED: ");
        tx_cat(s, bt.cur >= 0 ? set.bt_dev[bt.cur].name : "");
    } else if (dev == BLE_DEV_CONNECTING || dev == BLE_DEV_SEARCHING)
        tx_cpy(s, "CONNECTING");
    else
        tx_cpy(s, set.bt_ndev ? "ON, SEARCHING" : "ON, NO DEVICE SAVED");
}
static int bt_found_at(int i)                        /* the list's row i: its entry (the MIDI ones first) */
{
    int k, c = 0;
    for (k = 0; k < bt.nf; k++)
        if (bt.f[k].midi && c++ == i)
            return k;
    for (k = 0; k < bt.nf; k++)
        if (!bt.f[k].midi && c++ == i)
            return k;
    return -1;
}
static void bt_draw(void)
{
    char a[32], b[24];
    int i, n, y;
    if (bt.adding) {                                 /* + ADD's scan and list */
        o_header("ADD A DEVICE", "");
        o_ink = K_DIM;
        if (bt.adding == 1) {
            tx_cpy(a, "SCANNING ");
            tx_num(a + tx_len(a), (int)((BT_SCAN_MS - (plat_ms() - bt.add_t) + 999u) / 1000u));
            tx_cat(a, " S");
        } else
            tx_cpy(a, bt.nf ? "OCT+: SAVE AND CONNECT" : "NOTHING FOUND: OCT+ AGAIN");
        o_ts(0, 12, a, 1);
        o_ink = K_FG;
        for (i = 0; i < bt.nf && i < 10; i++) {    /* (the MIDI ones first) */
            int j = bt_found_at(i);
            if (j < 0)
                break;
            y = 21 + i * 7;
            tx_cpy(a, bt.f[j].name[0] ? bt.f[j].name : "(NO NAME)");
            tx_cpy(b, bt.f[j].midi ? "MIDI" : "");
            if (i == bt.add_i) {
                o_rect(0, y - 1, OW, 7, 1);
                o_ts(2, y, a, 0);
                o_ts(OW - o_ws(b) - 2, y, b, 0);
            } else {
                o_ts(2, y, a, 1);
                o_ts(OW - o_ws(b) - 2, y, b, 1);
            }
        }
        o_ink = K_DIM;
        o_ts(0, 115, "SELECT  OCT+ SAVE  OCT- BACK", 1);
        o_ink = K_FG;
        return;
    }
    o_header("BLUETOOTH", "");
    o_ink = K_ACC;
    bt_state_str(a);
    o_ts(0, 12, a, 1);
    o_ink = K_FG;
    n = bt_nrows();
    if (bt.row < bt.top)
        bt.top = bt.row;
    if (bt.row > bt.top + 10)
        bt.top = (uint8_t)(bt.row - 10);
    for (i = bt.top; i < n && i < bt.top + 11; i++) {
        y = 21 + (i - bt.top) * 7;
        bt_row_str(i, a, b);
        if (i == bt.row) {
            o_rect(0, y - 1, OW, 7, 1);
            o_ts(2, y, a, 0);
            o_ts(OW - o_ws(b) - 2, y, b, 0);
        } else {
            o_ts(2, y, a, 1);
            o_ts(OW - o_ws(b) - 2, y, b, 1);
        }
    }
    o_ink = K_DIM;
    if (eng.ext_clock && clk_jit_ms > 0.0f) {      /* (0.30) the clock followed: its scatter, the follower's error */
        tx_cpy(a, "CLOCK JIT ");
        tx_num(a + tx_len(a), (int)(clk_jit_ms * 10.0f + 0.5f));
        tx_cat(a, "  ERR ");
        tx_num(a + tx_len(a), (int)(fm_sqrtf(clk_err_ms) * 10.0f + 0.5f));
        tx_cat(a, " (0.1 MS)");
        o_ts(0, 101, a, 1);
    }
    if (bt.forget) {
        tx_cpy(a, "FORGET ");
        tx_cat(a, set.bt_dev[bt.forget - 1].name);
        tx_cat(a, "? OCT+");
        o_ink = K_ACC;
        o_ts(0, 108, a, 1);
        o_ink = K_DIM;
    } else if (bt.err[0])
        o_ts(0, 108, bt.err, 1);
    o_ts(0, 115, bt.row >= BR_DEV ? "OCT+ USE  K1 LEFT FORGET" : "SELECT ROW  K1 VALUE  OCT-", 1);
    o_ink = K_FG;
}
static void bt_enc(int e, int d)                     /* the page's knobs */
{
    if (bt.adding) {
        if (e == EN_SELECT && bt.nf)
            bt.add_i = (uint8_t)clamp(bt.add_i + d, 0, bt.nf - 1);
        ui.redraw = 1;
        return;
    }
    if (e == EN_SELECT) {
        bt.row = (uint8_t)clamp(bt.row + d, 0, bt_nrows() - 1);
        bt.forget = 0;
    } else if (e == EN_K1) {
        switch (bt.row) {
        case BR_ON: if ((d > 0) != bt.on) bt_set_on(d > 0); break;
        case BR_AUTO: set.bt_off = (uint8_t)(d < 0); set_dirty = 1; break;
        case BR_ROLE: set.bt_role = (uint8_t)(d > 0); set_dirty = 1; bt.cur = -1; break;
        case BR_MODE: set.bt_mode = (uint8_t)clamp(set.bt_mode + d, 0, 2); set_dirty = 1; break;
        case BR_RECON: set.bt_recon = (uint8_t)clamp(set.bt_recon + d, 0, 2); set_dirty = 1; break;
        default:
            if (bt.row - BR_DEV < set.bt_ndev && d < 0)
                bt.forget = (uint8_t)(bt.row - BR_DEV + 1);
            break;
        }
        if (bt.row >= bt_nrows())
            bt.row = (uint8_t)(bt_nrows() - 1);
    }
    ui.redraw = 1;
}
static void bt_oct(int d)                           /* OCT+ acts, OCT- goes back */
{
    ui.redraw = 1;
    if (bt.adding) {
        if (d < 0) {
            bt.adding = 0;
            bt_scan(0);
        } else if (bt.adding == 2 && !bt.nf) {       /* NOTHING FOUND: again */
            bt.adding = 1;
            bt.add_t = plat_ms();
            bt.scanning = 0;
        } else if (bt.nf) {                          /* save the pick and connect it */
            int j = bt_found_at(bt.add_i), k;
            if (j < 0)
                return;
            if (set.bt_ndev >= 8u)
                set.bt_ndev = 7;                     /* (full: the last one gives way) */
            for (k = set.bt_ndev; k > 0; k--)
                set.bt_dev[k] = set.bt_dev[k - 1];
            memset(&set.bt_dev[0], 0, sizeof set.bt_dev[0]);
            tx_cpy(set.bt_dev[0].name, bt.f[j].name[0] ? bt.f[j].name : "DEVICE");
            memcpy(set.bt_dev[0].addr, bt.f[j].addr, 6);
            set.bt_dev[0].addr_rand = bt.f[j].addr_rand;
            set.bt_dev[0].kind = (uint8_t)bt_kind(set.bt_dev[0].name);
            for (k = 7; k > 0; k--)
                bt.seen_t[k] = bt.seen_t[k - 1];
            bt.seen_t[0] = plat_ms() | 1u;
            set.bt_ndev++;
            set_dirty = 1;
            bt.adding = 0;
            bt_scan(0);
            bt.retry_t = plat_ms();
            bt.retry_ms = 0;
            if (bt_connect_saved(0) != 0)
                tx_cpy(bt.err, "CONNECT FAILED");
            app_say("SAVED", set.bt_dev[0].name);
            bt.row = BR_DEV;
        }
        return;
    }
    if (d < 0) {                                     /* back to the MENU */
        if (bt.forget)
            bt.forget = 0;
        else
            ui.ov = OV_MENU;
        return;
    }
    if (bt.forget) {                                 /* FORGET: yes */
        int i = bt.forget - 1, k;
        if (bt.cur == i && plat_ble_device() != BLE_DEV_OFF)
            plat_ble_disconnect();
        app_say("FORGOTTEN", set.bt_dev[i].name);
        for (k = i; k + 1 < set.bt_ndev; k++) {
            set.bt_dev[k] = set.bt_dev[k + 1];
            bt.seen_t[k] = bt.seen_t[k + 1];
        }
        set.bt_ndev--;
        bt.cur = -1;
        bt.forget = 0;
        set_dirty = 1;
        if (bt.row >= bt_nrows())
            bt.row = (uint8_t)(bt_nrows() - 1);
        return;
    }
    if (bt.row == BR_ON)
        bt_set_on(!bt.on);
    else if (bt.row >= BR_DEV && bt.row - BR_DEV < set.bt_ndev && set.bt_role == 1) {   /* use it now */
        int i = bt.row - BR_DEV;
        if (!bt.on || plat_ble_state() != BLE_READY) {
            tx_cpy(bt.err, "BLUETOOTH IS OFF");
            return;
        }
        if (plat_ble_device() != BLE_DEV_OFF && plat_ble_device() != BLE_DEV_SCANNING)
            plat_ble_disconnect();
        bt_to_front(i);
        { uint32_t s = bt.seen_t[i]; int k; for (k = i; k > 0; k--) bt.seen_t[k] = bt.seen_t[k - 1]; bt.seen_t[0] = s; }
        bt_scan(0);
        if (bt_connect_saved(0) != 0)
            tx_cpy(bt.err, "CONNECT FAILED");
        bt.row = BR_DEV;
    } else if (bt.row == BR_DEV + set.bt_ndev && set.bt_role == 1) {   /* + ADD A DEVICE */
        if (!bt.on || plat_ble_state() != BLE_READY) {
            tx_cpy(bt.err, "BLUETOOTH IS OFF");
            return;
        }
        bt.adding = 1;
        bt.add_t = plat_ms();
        bt.nf = 0;
        bt.add_i = 0;
        bt.scanning = 0;
    }
}
