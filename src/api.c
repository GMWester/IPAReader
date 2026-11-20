/* Public API: voices, synthesis entry points, WAV encoding, analysis. */
#include "internal.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL _Thread_local
#endif

static THREAD_LOCAL int g_unknown;
void ipr_set_unknown(int n) { g_unknown = n; }
int ipr_unknown_count(void) { return g_unknown; }

const char *ipr_version(void) { return IPR_VERSION; }
void ipr_free(void *p) { free(p); }

const char *ipr_strerror(int e) {
    switch (e) {
    case IPR_OK: return "ok";
    case IPR_ERR_ARG: return "invalid argument";
    case IPR_ERR_MEMORY: return "out of memory";
    case IPR_ERR_IO: return "i/o error";
    case IPR_ERR_VOICE: return "invalid voice";
    case IPR_ERR_ABORTED: return "aborted by callback";
    }
    return "unknown error";
}

/* ------------------------------------------------------------------ */
/* Voices                                                              */
/* ------------------------------------------------------------------ */

static void voice_male(ipr_voice *v) {
    memset(v, 0, sizeof *v);
    strcpy(v->name, "male");
    v->pitch = 112; v->pitch_range = 1.0f; v->formant_scale = 1.0f;
    for (int i = 0; i < 5; i++) v->formant_shift[i] = 1.0f;
    v->bandwidth_scale = 1.0f; v->open_quotient = 0.55f; v->spectral_tilt = 0.25f;
    v->breathiness = 0.04f; v->jitter = 0.006f; v->shimmer = 0.03f;
    v->vibrato_depth = 0.06f; v->vibrato_rate = 5.0f;
    v->speed = 1.0f; v->volume = 1.0f; v->frication_gain = 1.0f;
}
static void voice_female(ipr_voice *v) {
    voice_male(v);
    strcpy(v->name, "female");
    v->pitch = 200; v->pitch_range = 1.15f; v->formant_scale = 1.13f;
    v->formant_shift[0] = 1.05f;
    v->bandwidth_scale = 1.1f; v->open_quotient = 0.65f; v->spectral_tilt = 0.35f;
    v->breathiness = 0.09f; v->jitter = 0.005f;
    v->vibrato_depth = 0.08f;
}

typedef struct { const char *name; void (*fn)(ipr_voice *); } Builtin;
static const Builtin BUILTINS[] = { {"male", voice_male}, {"female", voice_female} };

int ipr_voice_builtin_count(void) { return (int)(sizeof BUILTINS / sizeof BUILTINS[0]); }
const char *ipr_voice_builtin_name(int i) {
    return (i >= 0 && i < ipr_voice_builtin_count()) ? BUILTINS[i].name : NULL;
}

static int ieq(const char *a, const char *b) {
    while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0; a++; b++; }
    return *a == *b;
}

int ipr_voice_builtin(const char *name, ipr_voice *v) {
    if (!v) return IPR_ERR_ARG;
    if (!name || !*name) name = "male";
    for (int i = 0; i < ipr_voice_builtin_count(); i++)
        if (ieq(name, BUILTINS[i].name)) { BUILTINS[i].fn(v); return IPR_OK; }
    return IPR_ERR_VOICE;
}

typedef struct { const char *key; size_t off; float lo, hi; } VKey;
#define VK(k, f, lo, hi) {k, offsetof(ipr_voice, f), lo, hi}
static const VKey VKEYS[] = {
    VK("pitch", pitch, 30, 600), VK("pitch_range", pitch_range, 0, 4),
    VK("formant_scale", formant_scale, 0.6f, 1.8f), VK("bandwidth_scale", bandwidth_scale, 0.3f, 4),
    VK("open_quotient", open_quotient, 0.3f, 0.95f), VK("spectral_tilt", spectral_tilt, 0, 1),
    VK("breathiness", breathiness, 0, 1), VK("jitter", jitter, 0, 0.1f), VK("shimmer", shimmer, 0, 0.5f),
    VK("vibrato_depth", vibrato_depth, 0, 3), VK("vibrato_rate", vibrato_rate, 0, 15),
    VK("speed", speed, 0.1f, 6), VK("volume", volume, 0, 8), VK("frication_gain", frication_gain, 0, 8),
    VK("f1_shift", formant_shift[0], 0.5f, 2), VK("f2_shift", formant_shift[1], 0.5f, 2),
    VK("f3_shift", formant_shift[2], 0.5f, 2), VK("f4_shift", formant_shift[3], 0.5f, 2),
    VK("f5_shift", formant_shift[4], 0.5f, 2),
};

static int voice_validate(ipr_voice *v) {
    for (size_t i = 0; i < sizeof VKEYS / sizeof VKEYS[0]; i++) {
        float *f = (float *)((char *)v + VKEYS[i].off);
        if (!(*f >= VKEYS[i].lo && *f <= VKEYS[i].hi)) return IPR_ERR_VOICE;
    }
    return IPR_OK;
}

int ipr_voice_parse(const char *text, ipr_voice *v) {
    if (!text || !v) return IPR_ERR_ARG;
    ipr_voice tmp;
    voice_male(&tmp);
    strcpy(tmp.name, "custom");
    const char *p = text;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char line[256];
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len); line[len] = 0;
        p += len + (e ? 1 : 0);
        char *h = strchr(line, '#'); if (h) *h = 0;
        char *eq = strchr(line, '=');
        char *k = line;
        while (isspace((unsigned char)*k)) k++;
        if (!*k) continue;
        if (!eq) return IPR_ERR_VOICE;
        *eq = 0;
        char *val = eq + 1;
        char *ke = eq - 1;
        while (ke >= k && isspace((unsigned char)*ke)) *ke-- = 0;
        while (isspace((unsigned char)*val)) val++;
        char *ve = val + strlen(val) - 1;
        while (ve >= val && isspace((unsigned char)*ve)) *ve-- = 0;
        if (ieq(k, "base")) {
            char keep[32]; strcpy(keep, tmp.name);
            if (ipr_voice_builtin(val, &tmp)) return IPR_ERR_VOICE;
            strcpy(tmp.name, keep);
            continue;
        }
        if (ieq(k, "name")) { snprintf(tmp.name, sizeof tmp.name, "%s", val); continue; }
        int found = 0;
        for (size_t i = 0; i < sizeof VKEYS / sizeof VKEYS[0]; i++) {
            if (ieq(k, VKEYS[i].key)) {
                char *end;
                double d = strtod(val, &end);
                if (end == val) return IPR_ERR_VOICE;
                *(float *)((char *)&tmp + VKEYS[i].off) = (float)d;
                found = 1;
                break;
            }
        }
        if (!found) return IPR_ERR_VOICE;
    }
    if (voice_validate(&tmp)) return IPR_ERR_VOICE;
    *v = tmp;
    return IPR_OK;
}

int ipr_voice_load(const char *path, ipr_voice *v) {
    if (!path || !v) return IPR_ERR_ARG;
    FILE *f = fopen(path, "rb");
    if (!f) return IPR_ERR_IO;
    char *buf = NULL; size_t n = 0, cap = 0;
    for (;;) {
        if (n + 4096 + 1 > cap) {
            cap = cap ? cap * 2 : 8192;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); fclose(f); return IPR_ERR_MEMORY; }
            buf = nb;
        }
        size_t r = fread(buf + n, 1, 4096, f);
        n += r;
        if (r < 4096) break;
    }
    fclose(f);
    buf[n] = 0;
    int rc = ipr_voice_parse(buf, v);
    if (rc == IPR_OK && strcmp(v->name, "custom") == 0) {
        /* name the voice after the file */
        const char *b = strrchr(path, '/');
        const char *b2 = strrchr(path, '\\');
        if (b2 > b) b = b2;
        b = b ? b + 1 : path;
        snprintf(v->name, sizeof v->name, "%s", b);
        char *dot = strrchr(v->name, '.'); if (dot) *dot = 0;
    }
    free(buf);
    return rc;
}

int ipr_voice_format(const ipr_voice *v, char *buf, size_t buflen) {
    if (!v) return IPR_ERR_ARG;
    char tmp[2048];
    int n = snprintf(tmp, sizeof tmp, "name = %s\n", v->name);
    for (size_t i = 0; i < sizeof VKEYS / sizeof VKEYS[0]; i++)
        n += snprintf(tmp + n, sizeof tmp - (size_t)n, "%s = %g\n", VKEYS[i].key,
                      *(const float *)((const char *)v + VKEYS[i].off));
    if (buf && buflen) snprintf(buf, buflen, "%s", tmp);
    return n;
}

/* ------------------------------------------------------------------ */
/* Synthesis                                                           */
/* ------------------------------------------------------------------ */

void ipr_options_default(ipr_options *o) {
    memset(o, 0, sizeof *o);
    o->sample_rate = 22050; o->speed = 1.0f; o->volume = 1.0f;
}

typedef struct {
    ipr_audio_cb cb16; void *user;
    int16_t buf[1024];
    /* collectors */
    float *fl; size_t fn, fcap;
    int err;
} Sink;

static int sink_float(const float *x, size_t n, void *u) {
    Sink *s = u;
    if (s->fn + n > s->fcap) {
        size_t nc = s->fcap ? s->fcap * 2 : 65536;
        while (nc < s->fn + n) nc *= 2;
        float *p = realloc(s->fl, nc * sizeof *p);
        if (!p) { s->err = IPR_ERR_MEMORY; return 1; }
        s->fl = p; s->fcap = nc;
    }
    memcpy(s->fl + s->fn, x, n * sizeof *x);
    s->fn += n;
    return 0;
}

static int16_t to16(float x) {
    float y = x * 32767.0f;
    if (y > 32767) y = 32767;
    if (y < -32768) y = -32768;
    return (int16_t)lrintf(y);
}

static int sink_stream(const float *x, size_t n, void *u) {
    Sink *s = u;
    while (n) {
        size_t k = n > 1024 ? 1024 : n;
        for (size_t i = 0; i < k; i++) s->buf[i] = to16(x[i]);
        if (s->cb16(s->buf, k, s->user)) return 1;
        x += k; n -= k;
    }
    return 0;
}

static int run(const char *ipa, const ipr_voice *voice, const ipr_options *opts, ipr_float_cb cb, void *user) {
    if (!ipa) return IPR_ERR_ARG;
    ipr_voice v;
    ipr_options o;
    if (voice) { v = *voice; if (voice_validate(&v)) return IPR_ERR_VOICE; }
    else voice_male(&v);
    if (opts) o = *opts; else ipr_options_default(&o);
    if (o.sample_rate < 8000 || o.sample_rate > 96000) return IPR_ERR_ARG;
    if (!(o.speed > 0)) o.speed = 1.0f;
    SegList segs;
    int rc = ipr_parse(ipa, &segs);
    if (rc) return rc;
    ipr_set_unknown(segs.unknown);
    Plan plan;
    rc = ipr_plan(&segs, &v, &o, &plan);
    ipr_seglist_free(&segs);
    if (rc) return rc;
    rc = ipr_render(&plan, &v, o.sample_rate, 0.9f * o.volume, cb, user);
    ipr_plan_free(&plan);
    return rc;
}

int ipr_synth_float(const char *ipa, const ipr_voice *voice, const ipr_options *opts, float **samples, size_t *n) {
    if (!samples || !n) return IPR_ERR_ARG;
    Sink s; memset(&s, 0, sizeof s);
    int rc = run(ipa, voice, opts, sink_float, &s);
    if (rc == IPR_ERR_ABORTED && s.err) rc = s.err;
    if (rc) { free(s.fl); return rc; }
    if (!s.fl) { s.fl = malloc(sizeof(float)); if (!s.fl) return IPR_ERR_MEMORY; }
    *samples = s.fl; *n = s.fn;
    return IPR_OK;
}

int ipr_synth(const char *ipa, const ipr_voice *voice, const ipr_options *opts, int16_t **samples, size_t *n) {
    float *f; size_t fn;
    if (!samples || !n) return IPR_ERR_ARG;
    int rc = ipr_synth_float(ipa, voice, opts, &f, &fn);
    if (rc) return rc;
    int16_t *o = malloc((fn ? fn : 1) * sizeof *o);
    if (!o) { free(f); return IPR_ERR_MEMORY; }
    for (size_t i = 0; i < fn; i++) o[i] = to16(f[i]);
    free(f);
    *samples = o; *n = fn;
    return IPR_OK;
}

int ipr_synth_stream(const char *ipa, const ipr_voice *voice, const ipr_options *opts, ipr_audio_cb cb, void *user) {
    if (!cb) return IPR_ERR_ARG;
    Sink s; memset(&s, 0, sizeof s);
    s.cb16 = cb; s.user = user;
    return run(ipa, voice, opts, sink_stream, &s);
}

static void put16(uint8_t *p, uint32_t v) { p[0] = v & 255; p[1] = (v >> 8) & 255; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

int ipr_wav_encode(const int16_t *x, size_t n, int sr, uint8_t **wav, size_t *len) {
    if ((!x && n) || !wav || !len || n > 0x7FFFFFF0u / 2) return IPR_ERR_ARG;
    size_t L = 44 + n * 2;
    uint8_t *b = malloc(L);
    if (!b) return IPR_ERR_MEMORY;
    memcpy(b, "RIFF", 4); put32(b + 4, (uint32_t)(L - 8)); memcpy(b + 8, "WAVEfmt ", 8);
    put32(b + 16, 16); put16(b + 20, 1); put16(b + 22, 1); put32(b + 24, (uint32_t)sr);
    put32(b + 28, (uint32_t)sr * 2); put16(b + 32, 2); put16(b + 34, 16);
    memcpy(b + 36, "data", 4); put32(b + 40, (uint32_t)(n * 2));
    for (size_t i = 0; i < n; i++) put16(b + 44 + i * 2, (uint16_t)x[i]);
    *wav = b; *len = L;
    return IPR_OK;
}

int ipr_synth_to_wav(const char *ipa, const ipr_voice *voice, const ipr_options *opts, const char *path) {
    if (!path) return IPR_ERR_ARG;
    int16_t *s; size_t n;
    int rc = ipr_synth(ipa, voice, opts, &s, &n);
    if (rc) return rc;
    uint8_t *w; size_t wl;
    rc = ipr_wav_encode(s, n, opts ? opts->sample_rate : 22050, &w, &wl);
    free(s);
    if (rc) return rc;
    FILE *f = fopen(path, "wb");
    if (!f) { free(w); return IPR_ERR_IO; }
    size_t wr = fwrite(w, 1, wl, f);
    int cl = fclose(f);
    free(w);
    return (wr == wl && cl == 0) ? IPR_OK : IPR_ERR_IO;
}

/* ------------------------------------------------------------------ */
/* Analysis                                                            */
/* ------------------------------------------------------------------ */

static int put_utf8(char *o, uint32_t c) {
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 63)); return 2; }
    if (c < 0x10000) { o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 63)); o[2] = (char)(0x80 | (c & 63)); return 3; }
    o[0] = (char)(0xF0 | (c >> 18)); o[1] = (char)(0x80 | ((c >> 12) & 63)); o[2] = (char)(0x80 | ((c >> 6) & 63)); o[3] = (char)(0x80 | (c & 63));
    return 4;
}

int ipr_analyze(const char *ipa, char *buf, size_t buflen) {
    if (!ipa) return IPR_ERR_ARG;
    SegList L;
    int rc = ipr_parse(ipa, &L);
    if (rc) return rc;
    ipr_set_unknown(L.unknown);
    size_t cap = 256 + (size_t)L.n * 200;
    char *out = malloc(cap);
    if (!out) { ipr_seglist_free(&L); return IPR_ERR_MEMORY; }
    size_t n = 0;
    static const char *PZ[] = {"word", "short", "minor", "major", "statement", "question", "exclamation", "comma"};
    for (int i = 0; i < L.n; i++) {
        Seg *s = &L.s[i];
        char sym[8] = {0};
        put_utf8(sym, s->sym ? s->sym : '?');
        if (s->kind == K_PAUSE) {
            n += (size_t)snprintf(out + n, cap - n, "pause   %s\n", PZ[s->pause_kind]);
            continue;
        }
        if (s->kind == K_VOWEL)
            n += (size_t)snprintf(out + n, cap - n, "vowel   %-3s height=%.2f back=%.2f round=%.2f", sym, s->height, s->back, s->round);
        else
            n += (size_t)snprintf(out + n, cap - n, "cons    %-3s %s %s %s%s", sym, s->voiced ? "voiced" : "voiceless",
                                  ipr_place_name(s->place), ipr_manner_name(s->manner), s->sibilant ? " sibilant" : "");
        if (s->kind == K_CONS && s->manner == M_AFFRICATE)
            n += (size_t)snprintf(out + n, cap - n, " release=%s%s", ipr_place_name(s->fric_place), s->fric_lateral ? "-lateral" : "");
        if (s->length != 1.0f) n += (size_t)snprintf(out + n, cap - n, " length=%.2f", s->length);
        if (s->stress > 0) n += (size_t)snprintf(out + n, cap - n, " stress=%s", s->stress == 2 ? "primary" : "secondary");
        if (s->nasal > 0) n += (size_t)snprintf(out + n, cap - n, " nasalized");
        if (s->rhotic > 0) n += (size_t)snprintf(out + n, cap - n, " rhotic");
        if (s->aspirated) n += (size_t)snprintf(out + n, cap - n, s->aspirated == 2 ? " breathy-aspirated" : " aspirated");
        if (s->preaspirated) n += (size_t)snprintf(out + n, cap - n, " preaspirated");
        if (s->prenasal) n += (size_t)snprintf(out + n, cap - n, " prenasalized");
        if (s->breathy) n += (size_t)snprintf(out + n, cap - n, " breathy");
        if (s->creaky) n += (size_t)snprintf(out + n, cap - n, " creaky");
        if (s->ejective) n += (size_t)snprintf(out + n, cap - n, " ejective");
        if (s->unreleased) n += (size_t)snprintf(out + n, cap - n, " unreleased");
        if (s->labialized) n += (size_t)snprintf(out + n, cap - n, " labialized");
        if (s->palatalized) n += (size_t)snprintf(out + n, cap - n, " palatalized");
        if (s->velarized) n += (size_t)snprintf(out + n, cap - n, " velarized");
        if (s->pharyngealized) n += (size_t)snprintf(out + n, cap - n, " pharyngealized");
        if (s->glottalized) n += (size_t)snprintf(out + n, cap - n, " glottalized");
        if (s->nasal_release) n += (size_t)snprintf(out + n, cap - n, " nasal-release");
        if (s->lateral_release) n += (size_t)snprintf(out + n, cap - n, " lateral-release");
        if (s->syllabic == 1) n += (size_t)snprintf(out + n, cap - n, " syllabic");
        if (s->syllabic == -1) n += (size_t)snprintf(out + n, cap - n, " non-syllabic");
        if (s->short_release) n += (size_t)snprintf(out + n, cap - n, " (superscript)");
        if (s->ntone) {
            n += (size_t)snprintf(out + n, cap - n, " tone=");
            for (int k = 0; k < s->ntone; k++) n += (size_t)snprintf(out + n, cap - n, "%d", (int)s->tone[k]);
        }
        n += (size_t)snprintf(out + n, cap - n, "\n");
    }
    if (L.unknown) n += (size_t)snprintf(out + n, cap - n, "unknown characters skipped: %d\n", L.unknown);
    ipr_seglist_free(&L);
    if (buf && buflen) snprintf(buf, buflen, "%s", out);
    free(out);
    return (int)n;
}
