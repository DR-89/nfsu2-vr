/* roam_selftest.c -- a deterministic byte stream from the free-roam core, for
 * the cross-platform check (tests/roam_x86_oracle.c): the same source built
 * for arm64 (linked into the oracle) and for 32-bit x86 (an ELF object run
 * under Unicorn) must produce the same bytes.  Covers every message
 * encoding, the CRC, the car-look record, matrix <-> quaternion, exp, sqrt,
 * the interpolator over a jittered stream, decoding of corrupted datagrams,
 * a two-session exchange (handshake, gossip, snapshots, car record) and the
 * encoder on NaN, infinite and out-of-range values.
 * No libm, no OS: built with -ffreestanding for x86.  tools/build_roam_asi.sh
 * also builds it as a Windows DLL (out/win/roam_selftest.dll) from the
 * plugin's own core object, for the same comparison through a PE loader.
 */
#ifdef ROAM_SELFTEST_EXTERN_CORE
/* the Windows build (tools/build_roam_asi.sh): linked with the very
 * roam_core.o and roam_libc.o of NFSU2FreeRoam.asi */
#include "roam_core.h"
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
#else
#include "../runtime/roam_core.c"
#endif

#if defined(ROAM_SELFTEST_FREESTANDING) && !defined(ROAM_SELFTEST_EXTERN_CORE)
/* the x86 object is linked by nobody: it brings its own */
#undef memcpy
#undef memset
#undef memmove
#undef memcmp
#undef strlen
void *memcpy(void *d, const void *s, size_t n){ uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d; }
void *memmove(void *d, const void *s, size_t n){
    uint8_t *a = d; const uint8_t *b = s;
    if (a < b) while (n--) *a++ = *b++; else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
void *memset(void *d, int c, size_t n){ uint8_t *a = d; while (n--) *a++ = (uint8_t)c; return d; }
int memcmp(const void *x, const void *y, size_t n){ const uint8_t *a = x, *b = y; for (; n; n--, a++, b++) if (*a != *b) return *a - *b; return 0; }
size_t strlen(const char *s){ size_t n = 0; while (s[n]) n++; return n; }
#endif

typedef struct { uint8_t *p; int n, cap; } Out;
static void o_bytes(Out *o, const void *b, int n){ for (int i = 0; i < n && o->n < o->cap; i++) o->p[o->n++] = ((const uint8_t *)b)[i]; }
static void o_u32(Out *o, uint32_t v){ uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; o_bytes(o, b, 4); }
static void o_f(Out *o, float f){ o_u32(o, rc_fbits(f)); }

static uint32_t st_rng;
static uint32_t st_rnd(void){ st_rng ^= st_rng << 13; st_rng ^= st_rng >> 17; st_rng ^= st_rng << 5; return st_rng; }
static float st_frnd(float lo, float hi){ return lo + (hi - lo) * (float)(st_rnd() % 100000u) / 100000.f; }

/* a two-session exchange through an in-memory queue */
static Out *g_o;
static rc_session g_a, g_b;
static struct { int to; uint32_t ip; uint16_t port; int len; uint8_t b[RC_MAX_DGRAM]; } g_q[64]; static int g_qn;
static void q_send(void *u, uint32_t ip, uint16_t port, const uint8_t *buf, int len){
    int from = (int)(intptr_t)u;
    o_u32(g_o, 0xD6A70000u | (uint32_t)from << 8 | (uint32_t)len);   /* every datagram, as sent */
    o_bytes(g_o, buf, len);
    if (g_qn < 64){ g_q[g_qn].to = port == 47210 ? 0 : 1; g_q[g_qn].ip = ip; g_q[g_qn].port = from ? 47211 : 47210;
                    g_q[g_qn].len = len; for (int i = 0; i < len; i++) g_q[g_qn].b[i] = buf[i]; g_qn++; }
}
static void q_event(void *u, int ev, int peer, const char *t){ (void)t; o_u32(g_o, 0xE7000000u | (uint32_t)(intptr_t)u << 16 | (uint32_t)ev << 8 | (uint32_t)(peer & 0xff)); }
static void q_pump(int64_t now){
    for (int k = 0; k < g_qn; k++) rc_receive(g_q[k].to ? &g_b : &g_a, g_q[k].ip, g_q[k].port, g_q[k].b, g_q[k].len, now);
    g_qn = 0;
}

int roam_selftest(uint8_t *out, int cap){
    Out o = { out, 0, cap }; g_o = &o;
    st_rng = 0x12345678u;
    uint8_t b[RC_MAX_DGRAM];
    /* 1. every message type with awkward values */
    for (int t = 1; t <= 8; t++){
        rc_msg m; memset(&m, 0, sizeof m);
        m.h.type = (uint8_t)t; m.h.sender = 0x9ABCDEF1u; m.h.seq = (uint16_t)(t * 4099);
        if (t == RC_MSG_HELLO){ m.u.hello.core_ver = 1; m.u.hello.platform = 2; m.u.hello.status = 2; m.u.hello.listen_port = 47210;
            m.u.hello.cfg_rev = 513; m.u.hello.room = rc_fnv1a("blue room"); memcpy(m.u.hello.name, "Zo\xeb ~ 42", 9);
            rc_name_clean(m.u.hello.name, "Zo\xeb ~ 42", 9); m.u.hello.n_seen = 3; m.u.hello.seen[0] = 1; m.u.hello.seen[1] = 0xFFFFFFFFu; m.u.hello.seen[2] = 77; }
        if (t == RC_MSG_PEERS){ m.u.peers.n = 2; m.u.peers.e[0].ip = 0xC0A80001u; m.u.peers.e[0].port = 1; m.u.peers.e[0].id = 2;
            m.u.peers.e[1].ip = 0x0A000102u; m.u.peers.e[1].port = 65535; m.u.peers.e[1].id = 0x80000000u; }
        if (t == RC_MSG_SNAP){ m.u.snap.status = 5; m.u.snap.epoch = 250; m.u.snap.clock = 3; m.u.snap.cfg_rev = 65535; m.u.snap.n = 3;
            for (int k = 0; k < 3; k++){ rc_state *s = &m.u.snap.s[k];
                s->t_us = 0xFFFFFF00u + (uint32_t)k * 50000u;
                for (int i = 0; i < 3; i++){ s->p[i] = st_frnd(-19000, 19000); s->v[i] = st_frnd(-90, 90); s->w[i] = st_frnd(-40, 40); }
                s->p[2] = st_frnd(-1900, 1900);
                float q[4] = { st_frnd(-1, 1), st_frnd(-1, 1), st_frnd(-1, 1), st_frnd(-1, 1) };
                for (int i = 0; i < 4; i++) s->q[i] = q[i];
                s->steer = st_frnd(-1.5f, 1.5f); s->throttle = st_frnd(-0.2f, 1.2f); s->brake = st_frnd(0, 1);
                s->rpm = st_frnd(0, 70000); s->speed = st_frnd(-400, 400); s->gear = (int8_t)(k - 1); s->bits = (uint8_t)(0x55 << k); } }
        if (t == RC_MSG_CFG){ static uint8_t d[1000]; for (int i = 0; i < 1000; i++) d[i] = (uint8_t)(i * 31);
            m.u.cfg.cfg_rev = 9; m.u.cfg.frag = 0; m.u.cfg.nfrag = 2; m.u.cfg.total = 1500; m.u.cfg.dlen = 1000; m.u.cfg.data = d; }
        if (t == RC_MSG_CFG_REQ){ m.u.cfgreq.cfg_rev = 9; m.u.cfgreq.have_mask = 2; }
        if (t == RC_MSG_PING || t == RC_MSG_PONG){ m.u.ping.t0 = 0xDEADBEEFu; m.u.ping.t1 = 1; m.u.ping.t2 = 0x7FFFFFFFu; }
        if (t == RC_MSG_BYE) m.u.bye.reason = 1;
        int n = rc_encode(&m, b, sizeof b);
        o_u32(&o, (uint32_t)n); if (n > 0) o_bytes(&o, b, n);
        /* and decoded again: the fields that come back */
        rc_msg d; int e = rc_decode(b, n, &d); o_u32(&o, (uint32_t)e);
        if (t == RC_MSG_SNAP && !e) for (int k = 0; k < d.u.snap.n; k++){ const rc_state *s = &d.u.snap.s[k];
            for (int i = 0; i < 3; i++){ o_f(&o, s->p[i]); o_f(&o, s->v[i]); o_f(&o, s->w[i]); }
            for (int i = 0; i < 4; i++) o_f(&o, s->q[i]);
            o_f(&o, s->steer); o_f(&o, s->throttle); o_f(&o, s->brake); o_f(&o, s->rpm); o_f(&o, s->speed); }
    }
    /* 2. corrupted datagrams: every byte flipped in turn */
    { rc_msg m; memset(&m, 0, sizeof m); m.h.type = RC_MSG_HELLO; m.h.sender = 5; memcpy(m.u.hello.name, "abc", 4); m.u.hello.n_seen = 1; m.u.hello.seen[0] = 9;
      int n = rc_encode(&m, b, sizeof b);
      for (int i = 0; i < n; i++){ uint8_t c = b[i]; b[i] ^= 0x41; rc_msg d; o_u32(&o, (uint32_t)rc_decode(b, n, &d)); b[i] = c; }
      for (int n2 = 0; n2 <= n; n2++){ rc_msg d; o_u32(&o, (uint32_t)rc_decode(b, n2, &d)); } }
    /* 3. car-look record */
    { rc_carcfg c, d; memset(&c, 0, sizeof c);
      c.type_hash = 0x11223344u; memcpy(c.type_name, "IMPREZAWRX", 11); c.ride_b[0] = 1; c.ride_b[3] = 1; c.n_parts = 90;
      for (int i = 0; i < 90; i++){ c.part[i].slot = (uint8_t)(i * 3 % 170); c.part[i].key = st_rnd(); c.part[i].owner = st_rnd(); }
      for (int i = 0; i < 90; i++) for (int j = 0; j < i; j++) if (c.part[j].slot == c.part[i].slot) c.part[i].slot = (uint8_t)(169 - i);
      c.has_perf = 1; for (int i = 0; i < RC_PERF_BYTES; i++) c.perf[i] = (uint8_t)st_rnd();
      uint8_t r[RC_CFG_MAX]; int n = rc_carcfg_encode(&c, r, sizeof r);
      o_u32(&o, (uint32_t)n); if (n > 0) o_bytes(&o, r, n);
      o_u32(&o, (uint32_t)rc_carcfg_decode(r, n, &d)); o_u32(&o, rc_crc32(r, (uint32_t)(n > 0 ? n : 0))); o_u32(&o, rc_fnv1a_bytes(r, (uint32_t)(n > 0 ? n : 0))); }
    /* 4. float helpers */
    for (int i = 0; i < 64; i++){ float x = st_frnd(0, 25); o_f(&o, rc_expneg(x)); o_f(&o, rc_sqrtf(x * 1000.f)); }
    for (int i = 0; i < 64; i++){ float y = st_frnd(-50, 50), x = st_frnd(-50, 50); o_f(&o, rc_atan2f(y, x)); o_f(&o, rc_atan2f(x, 0.f)); }
    for (int i = 0; i < 32; i++){
        float q[4] = { st_frnd(-1, 1), st_frnd(-1, 1), st_frnd(-1, 1), st_frnd(-1, 1) }, nn = rc_sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
        for (int k = 0; k < 4; k++) q[k] /= nn;
        float f[3], l[3], u[3], q2[4];
        rc_q2m(q, f, l, u); rc_m2q(f, l, u, q2);
        for (int k = 0; k < 3; k++){ o_f(&o, f[k]); o_f(&o, l[k]); o_f(&o, u[k]); }
        for (int k = 0; k < 4; k++) o_f(&o, q2[k]);
    }
    /* 5. the interpolator over a jittered, lossy, reordered stream */
    { static rc_interp ip; rc_interp_reset(&ip);
      float x = 100, y = -50, vx = 30, vy = 4, yaw = 0.3f;
      int64_t now = 1000000;
      for (int k = 0; k < 400; k++){
          now += 8333;
          if (k % 6 == 0){
              rc_state s; memset(&s, 0, sizeof s);
              s.t_us = (uint32_t)(now + 4000000000LL);
              vx += st_frnd(-0.5f, 0.5f); vy += st_frnd(-0.5f, 0.5f); x += vx * 0.05f; y += vy * 0.05f; yaw += st_frnd(-0.02f, 0.02f);
              s.p[0] = x; s.p[1] = y; s.p[2] = 12.5f; s.v[0] = vx; s.v[1] = vy; s.w[2] = 0.3f;
              s.q[2] = yaw * 0.5f; s.q[3] = 1.f - yaw * yaw * 0.125f;
              s.speed = rc_sqrtf(vx * vx + vy * vy); s.rpm = 3000 + 40 * s.speed;
              if (st_rnd() % 10u) rc_interp_insert(&ip, &s, 0, k > 300 ? 1 : 0, RC_ST_ROAM, now + (int64_t)(st_rnd() % 30000u), 1);
          }
          rc_pose P; int ok = rc_interp_eval(&ip, now, 8333, 100000, 250000, &P);
          o_u32(&o, (uint32_t)ok);
          if (ok){ for (int i = 0; i < 3; i++){ o_f(&o, P.p[i]); o_f(&o, P.v[i]); } for (int i = 0; i < 4; i++) o_f(&o, P.q[i]); o_u32(&o, (uint32_t)P.mode); }
      }
      o_u32(&o, (uint32_t)ip.n_interp); o_u32(&o, (uint32_t)ip.n_extrap); o_u32(&o, (uint32_t)ip.n_hold); o_u32(&o, (uint32_t)ip.n_snap); o_f(&o, ip.err_max); }
    /* 6. two sessions: handshake, car record, snapshots, ping, bye */
    { rc_config ca, cb; memset(&ca, 0, sizeof ca); memset(&cb, 0, sizeof cb);
      ca.self_id = 0xA0A0A0A0u; ca.port = 47210; memcpy(ca.name, "Alice", 6); ca.platform = 1; ca.test_mode = 1; ca.max_players = 8; ca.rate_hz = 20;
      cb = ca; cb.self_id = 0xB0B0B0B0u; cb.port = 47211; memcpy(cb.name, "Bob", 4); cb.platform = 2;
      rc_io ia = { (void *)(intptr_t)0, q_send, 0, q_event }, ib = { (void *)(intptr_t)1, q_send, 0, q_event };
      rc_session_init(&g_a, &ca, &ia); rc_session_init(&g_b, &cb, &ib); g_qn = 0;
      rc_add_peer(&g_a, 0x7F000001u, 47211, 1, 0);
      /* a valid car record of two fragments (120 parts, 1,135 bytes), and
       * before it one the receiver cannot read: noted once, not asked for
       * again, then the next revision arrives */
      uint8_t rec[RC_CFG_MAX]; for (int i = 0; i < 1300; i++) rec[i] = (uint8_t)(i ^ 0x5A);
      rc_set_cfg(&g_a, rec, 1300, 0);
      static rc_carcfg cc; memset(&cc, 0, sizeof cc); cc.type_hash = 0x1234567u; memcpy(cc.type_name, "MUSTANGGT", 10); cc.n_parts = 120;
      for (int i = 0; i < 120; i++){ cc.part[i].slot = (uint8_t)(i + 30); cc.part[i].key = 0x9E3779B9u * (uint32_t)(i + 1); cc.part[i].owner = 0x1234567u; }
      uint8_t rec2[RC_CFG_MAX]; int rn2 = rc_carcfg_encode(&cc, rec2, sizeof rec2);
      int64_t t = 0;
      for (int step = 0; step < 300; step++){
          t += 10000;
          rc_tick(&g_a, t); rc_tick(&g_b, t);
          if (step == 150) rc_set_cfg(&g_a, rec2, rn2, t);
          if (step > 50){
              rc_state s; memset(&s, 0, sizeof s); s.t_us = (uint32_t)t; s.p[0] = (float)step * 0.5f; s.v[0] = 50; s.q[3] = 1;
              rc_set_status(&g_a, RC_ST_ROAM); rc_push_state(&g_a, &s, t);
          }
          q_pump(t + 500);
      }
      o_u32(&o, (uint32_t)rc_established(&g_a)); o_u32(&o, (uint32_t)rc_established(&g_b));
      for (int i = 0; i < RC_TABLE; i++) if (g_b.peer[i].state == RC_PS_EST){ o_u32(&o, (uint32_t)g_b.peer[i].cfg_len); o_u32(&o, g_b.peer[i].cfg_bad_rev); }
      rc_shutdown(&g_a, 0); q_pump(t + 20000);
      o_u32(&o, (uint32_t)rc_established(&g_b)); }
    /* 7. edge values the game can hand the encoder: NaN, infinities and
     * values far out of range in every quantized field (a plain float-to-int
     * cast of these is undefined: arm64 saturates, x86 gives 0x80000000) */
    { rc_msg m; memset(&m, 0, sizeof m); m.h.type = RC_MSG_SNAP; m.h.sender = 1; m.u.snap.n = 3; m.u.snap.status = RC_ST_ROAM;
      volatile uint32_t nb = 0x7FC00000u, ib = 0x7F800000u; float nanv = rc_bitsf(nb), inf = rc_bitsf(ib);
      volatile float big = 3e6f, huge = 3e38f;
      float vals[8] = { nanv, inf, -inf, big, -big, huge, -huge, 2147483520.f };
      for (int k = 0; k < 3; k++){
          rc_state *s = &m.u.snap.s[k]; s->q[3] = 1;
          s->w[0] = vals[k]; s->w[1] = vals[3 + k]; s->w[2] = vals[5 + (k & 1)];
          s->steer = vals[(k + 1) % 8]; s->throttle = vals[(k + 2) % 8]; s->brake = vals[(k + 3) % 8];
          s->rpm = vals[(k + 4) % 8]; s->speed = vals[(k + 5) % 8];
      }
      int n = rc_encode(&m, b, sizeof b);
      o_u32(&o, 0xEDCE0000u | (uint32_t)n);
      if (n > 0) o_bytes(&o, b, n); }
    /* 8. state extensions (RC_HF_STATEX): wheel heights, nitrous, hydraulics, one
     * state without, edge values; decoded again, and the wheel heights
     * interpolated halfway between two states */
    { rc_msg m; memset(&m, 0, sizeof m); m.h.type = RC_MSG_SNAP; m.h.sender = 1; m.h.flags = RC_HF_STATEX;
      m.u.snap.n = 3; m.u.snap.status = RC_ST_ROAM;
      volatile uint32_t nb = 0x7FC00000u, ib = 0x7F800000u; float nanv = rc_bitsf(nb), inf = rc_bitsf(ib);
      for (int k = 0; k < 3; k++){
          rc_state *s = &m.u.snap.s[k]; s->q[3] = 1; s->t_us = 1000000u - 50000u * (uint32_t)k;
          s->ext = k != 1; s->nos = (uint8_t)(k * 120); s->hyd = (uint8_t)(0x1b * k); s->thumps = (uint8_t)(254 + k);
          for (int i = 0; i < 4; i++) s->wz[i] = st_frnd(-0.4f, 0.4f);
      }
      m.u.snap.s[2].wz[0] = nanv; m.u.snap.s[2].wz[1] = inf; m.u.snap.s[2].wz[2] = -inf; m.u.snap.s[2].wz[3] = 0.0005f;
      int n = rc_encode(&m, b, sizeof b);
      o_u32(&o, 0xE8000000u | (uint32_t)n);
      if (n > 0) o_bytes(&o, b, n);
      rc_msg d; int e = rc_decode(b, n, &d); o_u32(&o, (uint32_t)e);
      if (!e){
          for (int k = 0; k < d.u.snap.n; k++){ const rc_state *s = &d.u.snap.s[k];
              o_u32(&o, (uint32_t)s->thumps << 24 | (uint32_t)s->hyd << 16 | (uint32_t)s->ext << 8 | s->nos); for (int i = 0; i < 4; i++) o_f(&o, s->wz[i]); }
          static rc_interp ip; rc_interp_reset(&ip);
          rc_interp_insert(&ip, &d.u.snap.s[2], 0, 0, RC_ST_ROAM, 2000000, 0);
          rc_interp_insert(&ip, &d.u.snap.s[0], 0, 0, RC_ST_ROAM, 2000000, 1);
          rc_pose P; rc_interp_peek(&ip, 950000, 250000, &P);
          o_u32(&o, (uint32_t)P.thumps << 24 | (uint32_t)P.hyd << 16 | (uint32_t)P.ext << 8 | P.nos); for (int i = 0; i < 4; i++) o_f(&o, P.wz[i]);
      } }
    return o.n;
}
