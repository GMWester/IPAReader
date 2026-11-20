/* Segments -> timed synthesis parameter frames (1 frame = 1 ms). */
#include "internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Vowel formants: [height 0..6 open->close][front, central, back][unrounded, rounded][F1..F3] */
/* ------------------------------------------------------------------ */
static const float VF[7][3][2][3] = {
    /* open */       {{{800,1650,2600},{770,1450,2400}}, {{780,1350,2500},{740,1200,2400}}, {{750,1120,2500},{700,1000,2450}}},
    /* near-open */  {{{690,1700,2550},{680,1500,2400}}, {{690,1350,2500},{670,1200,2400}}, {{690,1150,2500},{640, 950,2450}}},
    /* open-mid */   {{{600,1850,2550},{590,1550,2350}}, {{590,1420,2500},{580,1220,2380}}, {{600,1180,2500},{570, 880,2450}}},
    /* mid */        {{{500,1950,2600},{490,1600,2300}}, {{500,1450,2500},{490,1230,2350}}, {{510,1200,2480},{480, 820,2400}}},
    /* close-mid */  {{{410,2080,2700},{400,1650,2250}}, {{410,1480,2500},{400,1250,2320}}, {{430,1220,2450},{400, 760,2380}}},
    /* near-close */ {{{330,2180,2800},{330,1720,2200}}, {{340,1520,2500},{330,1280,2300}}, {{350,1240,2420},{330, 720,2320}}},
    /* close */      {{{280,2280,2950},{280,1800,2150}}, {{300,1550,2500},{290,1300,2280}}, {{300,1260,2400},{290, 690,2280}}},
};

static float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static float lerp(float a, float b, float t) { return a + (b - a) * t; }

typedef struct { float F[5], B[5]; } Formants;

static void vowel_formants(float h, float b, float r, float rh, Formants *o) {
    float hc = clampf(h, 0, 1), bc = clampf(b, 0, 1), rc = clampf(r, 0, 1);
    float hi = hc * 6.0f;
    int h0 = (int)floorf(hi); if (h0 > 5) h0 = 5;
    float ht = hi - h0;
    float bi = bc * 2.0f;
    int b0 = (int)floorf(bi); if (b0 > 1) b0 = 1;
    float bt = bi - b0;
    for (int k = 0; k < 3; k++) {
        float v[2];
        for (int rr = 0; rr < 2; rr++) {
            float a = lerp(VF[h0][b0][rr][k], VF[h0][b0 + 1][rr][k], bt);
            float c = lerp(VF[h0 + 1][b0][rr][k], VF[h0 + 1][b0 + 1][rr][k], bt);
            v[rr] = lerp(a, c, ht);
        }
        o->F[k] = lerp(v[0], v[1], rc);
        if (r > 1.0f && k > 0) o->F[k] -= 80.0f * (r - 1.0f) / 0.3f; /* over-rounded */
    }
    /* extrapolation for raised/lowered/advanced beyond the chart */
    if (h > 1) o->F[0] -= (h - 1) * 250;
    if (h < 0) o->F[0] += (-h) * 300;
    if (b < 0) o->F[1] += (-b) * 400;
    if (b > 1) o->F[1] -= (b - 1) * 300;
    if (rh > 0) {
        o->F[2] = lerp(o->F[2], 1650, rh);
        o->F[1] = lerp(o->F[1], 1350, rh * 0.5f);
    }
    o->F[0] = clampf(o->F[0], 200, 1000);
    o->F[3] = 3500 + (1 - bc) * 150;
    o->F[4] = 4500;
    o->B[0] = 55 + 0.06f * (o->F[0] - 250);
    o->B[1] = 55 + 0.035f * o->F[1];
    o->B[2] = 40 + 0.045f * o->F[2];
    o->B[3] = 250;
    o->B[4] = 300;
}

/* consonant place loci F1, F2, F3 and coarticulation coefficient */
static const float LOCUS[P_COUNT][4] = {
    /* BILABIAL */     {220,  850, 2250, .55f},
    /* LABIODENTAL */  {230, 1100, 2350, .50f},
    /* LINGUOLABIAL */ {230, 1300, 2400, .50f},
    /* DENTAL */       {230, 1500, 2600, .45f},
    /* ALVEOLAR */     {220, 1700, 2650, .40f},
    /* POSTALV */      {240, 1950, 2550, .30f},
    /* RETROFLEX */    {260, 1650, 1900, .30f},
    /* ALVPAL */       {240, 2100, 2850, .25f},
    /* PALATAL */      {240, 2250, 3000, .20f},
    /* VELAR */        {240, 1700, 2100, .70f},
    /* UVULAR */       {450, 1150, 2400, .40f},
    /* PHARYNGEAL */   {850, 1150, 2500, .30f},
    /* EPIGLOTTAL */   {800, 1200, 2500, .30f},
    /* GLOTTAL */      {500, 1500, 2500, 1.0f},
    /* LABVELAR */     {230,  750, 2200, .30f},
    /* LABPAL */       {240, 1800, 2200, .30f},
};

/* nasal zero per place */
static const float NASAL_ZERO[P_COUNT] = {
    1100, 1100, 1300, 1600, 1700, 1900, 1600, 2100, 2100, 2700, 2000, 2000, 2000, 2000, 1500, 1800
};

/* Frication / burst spectra: up to 4 peaks (freq, bw, dB) + bypass dB + level dB */
typedef struct { float f[4], bw[4], db[4]; float bypass_db; float level_db; } Spec;

#define NONE -99.0f
static const Spec FRIC_SPEC[P_COUNT] = {
    /* BILABIAL */     {{1000, 3000, 0, 0}, {700, 1500, 1, 1}, {0, -10, NONE, NONE}, -6, -30},
    /* LABIODENTAL */  {{6000, 1400, 3500, 0}, {2500, 800, 1500, 1}, {-2, -12, -8, NONE}, 0, -31},
    /* LINGUOLABIAL */ {{5000, 2800, 0, 0}, {2500, 1500, 1, 1}, {-2, -10, NONE, NONE}, -2, -25},
    /* DENTAL */       {{5500, 2800, 7500, 0}, {2000, 1500, 2000, 1}, {-2, -10, -4, NONE}, -3, -30},
    /* ALVEOLAR */     {{5300, 7500, 3300, 0}, {1000, 1800, 600, 1}, {0, -4, -18, NONE}, -30, -11},
    /* POSTALV */      {{2700, 4000, 6000, 0}, {450, 900, 1500, 1}, {0, -6, -14, NONE}, -30, -7},
    /* RETROFLEX */    {{2300, 3300, 5000, 0}, {400, 700, 1500, 1}, {0, -6, -16, NONE}, -30, -12},
    /* ALVPAL */       {{3300, 4800, 2200, 0}, {600, 1200, 500, 1}, {0, -6, -14, NONE}, -30, -12},
    /* PALATAL */      {{3600, 5000, 2600, 0}, {600, 1200, 500, 1}, {0, -8, -14, NONE}, -30, -15},
    /* VELAR */        {{1800, 3200, 0, 0}, {400, 800, 1, 1}, {0, -8, NONE, NONE}, -30, -16},
    /* UVULAR */       {{1300, 2600, 0, 0}, {300, 700, 1, 1}, {0, -8, NONE, NONE}, -30, -16},
    /* PHARYNGEAL */   {{1100, 2600, 0, 0}, {400, 800, 1, 1}, {0, -10, NONE, NONE}, -30, -22},
    /* EPIGLOTTAL */   {{900, 2500, 0, 0}, {500, 1000, 1, 1}, {0, -8, NONE, NONE}, -30, -18},
    /* GLOTTAL */      {{1500, 0, 0, 0}, {1000, 1, 1, 1}, {NONE, NONE, NONE, NONE}, -40, -60},
    /* LABVELAR */     {{1200, 2500, 0, 0}, {600, 1200, 1, 1}, {0, -10, NONE, NONE}, -10, -24},
    /* LABPAL */       {{2000, 3500, 0, 0}, {800, 1200, 1, 1}, {0, -10, NONE, NONE}, -10, -24},
};
static const Spec LATFRIC_SPEC = {{3200, 5000, 1800, 0}, {800, 1500, 600, 1}, {0, -4, -14, NONE}, -20, -15};
static const Spec BURST_SPEC[P_COUNT] = {
    /* BILABIAL */     {{900, 2500, 0, 0}, {700, 1500, 1, 1}, {0, -10, NONE, NONE}, -6, -15},
    /* LABIODENTAL */  {{1200, 4000, 0, 0}, {900, 2000, 1, 1}, {0, -8, NONE, NONE}, -4, -16},
    /* LINGUOLABIAL */ {{1500, 4000, 0, 0}, {900, 2000, 1, 1}, {0, -4, NONE, NONE}, -4, -12},
    /* DENTAL */       {{4000, 6500, 2800, 0}, {1500, 2000, 800, 1}, {0, -3, -8, NONE}, -12, -8},
    /* ALVEOLAR */     {{4300, 6500, 3000, 0}, {1500, 2000, 800, 1}, {0, -4, -6, NONE}, -14, -7},
    /* POSTALV */      {{3000, 4500, 0, 0}, {800, 1200, 1, 1}, {0, -5, NONE, NONE}, -20, -7},
    /* RETROFLEX */    {{2700, 3800, 0, 0}, {800, 1200, 1, 1}, {0, -6, NONE, NONE}, -20, -8},
    /* ALVPAL */       {{3300, 4600, 0, 0}, {800, 1200, 1, 1}, {0, -4, NONE, NONE}, -20, -7},
    /* PALATAL */      {{3300, 4300, 0, 0}, {800, 1200, 1, 1}, {0, -4, NONE, NONE}, -20, -7},
    /* VELAR */        {{2000, 3500, 0, 0}, {500, 900, 1, 1}, {0, -10, NONE, NONE}, -30, -7},
    /* UVULAR */       {{1300, 2500, 0, 0}, {400, 800, 1, 1}, {0, -10, NONE, NONE}, -30, -9},
    /* PHARYNGEAL */   {{1000, 2500, 0, 0}, {500, 900, 1, 1}, {0, -10, NONE, NONE}, -30, -14},
    /* EPIGLOTTAL */   {{900, 2500, 0, 0}, {500, 900, 1, 1}, {0, -10, NONE, NONE}, -30, -12},
    /* GLOTTAL */      {{1500, 0, 0, 0}, {1000, 1, 1, 1}, {NONE, NONE, NONE, NONE}, -40, -40},
    /* LABVELAR */     {{1000, 1800, 0, 0}, {600, 500, 1, 1}, {0, -4, NONE, NONE}, -10, -12},
    /* LABPAL */       {{1000, 2500, 0, 0}, {600, 800, 1, 1}, {0, -4, NONE, NONE}, -10, -12},
};

static float db2lin(float db) { return db <= -90 ? 0.0f : powf(10.0f, db / 20.0f); }

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static int is_obstruent(const Seg *s) {
    if (s->kind != K_CONS) return 0;
    switch (s->manner) {
    case M_PLOSIVE: case M_FRIC: case M_LATFRIC: case M_AFFRICATE: case M_IMPLOSIVE: case M_CLICK: return 1;
    }
    return 0;
}
static int is_stoplike(const Seg *s) {
    return s->kind == K_CONS && (s->manner == M_PLOSIVE || s->manner == M_AFFRICATE ||
                                 s->manner == M_IMPLOSIVE || s->manner == M_CLICK);
}
static int is_sonorant_cons(const Seg *s) {
    return s->kind == K_CONS && !is_obstruent(s);
}
static int is_nucleus(const Seg *s) {
    if (s->kind == K_VOWEL) return s->syllabic != -1;
    if (s->kind == K_CONS) return s->syllabic == 1;
    return 0;
}

/* own formant target of a segment (before coarticulation) */
static void seg_target(const Seg *s, Formants *o) {
    if (s->kind == K_VOWEL) {
        vowel_formants(s->height, s->back, s->round, s->rhotic, o);
    } else {
        const float *L = LOCUS[s->place];
        o->F[0] = L[0]; o->F[1] = L[1]; o->F[2] = L[2];
        o->F[3] = 3500; o->F[4] = 4500;
        o->B[0] = 90; o->B[1] = 120; o->B[2] = 180; o->B[3] = 250; o->B[4] = 300;
        int p = s->place;
        switch (s->manner) {
        case M_NASAL:
            o->F[0] = 250; o->B[0] = 80;
            if (p == P_BILABIAL || p == P_LABIODENTAL) { o->F[1] = 1100; o->F[2] = 2250; }
            else if (p == P_ALVEOLAR || p == P_DENTAL) { o->F[1] = 1550; o->F[2] = 2600; }
            else if (p == P_RETROFLEX) { o->F[1] = 1500; o->F[2] = 2000; }
            else if (p == P_PALATAL || p == P_ALVPAL || p == P_POSTALV) { o->F[1] = 2100; o->F[2] = 2900; }
            else if (p == P_VELAR) { o->F[1] = 1750; o->F[2] = 2200; }
            else if (p == P_UVULAR) { o->F[1] = 1200; o->F[2] = 2400; }
            else if (p == P_LABVELAR) { o->F[1] = 1000; o->F[2] = 2200; }
            o->B[1] = 150; o->B[2] = 200;
            break;
        case M_LATAPPROX: case M_LATTAP:
            o->F[0] = 360; o->B[0] = 100; o->B[1] = 150; o->B[2] = 250;
            if (p == P_RETROFLEX) { o->F[1] = 1300; o->F[2] = 1900; }
            else if (p == P_PALATAL || p == P_ALVPAL || p == P_POSTALV) { o->F[0] = 300; o->F[1] = 2050; o->F[2] = 2900; }
            else if (p == P_VELAR || p == P_UVULAR) { o->F[0] = 340; o->F[1] = 1150; o->F[2] = 2400; }
            else { o->F[1] = 1250; o->F[2] = 2700; }
            break;
        case M_APPROX:
            o->B[0] = 70; o->B[1] = 110; o->B[2] = 180;
            if (p == P_ALVEOLAR || p == P_DENTAL || p == P_POSTALV) { o->F[0] = 380; o->F[1] = 1150; o->F[2] = 1550; }
            else if (p == P_RETROFLEX) { o->F[0] = 380; o->F[1] = 1250; o->F[2] = 1450; }
            else if (p == P_LABIODENTAL) { o->F[0] = 320; o->F[1] = 1350; o->F[2] = 2350; }
            else if (p == P_PALATAL || p == P_ALVPAL) { o->F[0] = 260; o->F[1] = 2150; o->F[2] = 3000; }
            else if (p == P_LABVELAR || p == P_BILABIAL) { o->F[0] = 290; o->F[1] = 650; o->F[2] = 2200; }
            else if (p == P_LABPAL) { o->F[0] = 270; o->F[1] = 1850; o->F[2] = 2250; }
            else if (p == P_VELAR) { o->F[0] = 300; o->F[1] = 1250; o->F[2] = 2450; }
            else if (p == P_UVULAR) { o->F[0] = 450; o->F[1] = 1150; o->F[2] = 2400; }
            else if (p == P_PHARYNGEAL || p == P_EPIGLOTTAL) { o->F[0] = 800; o->F[1] = 1150; o->F[2] = 2500; }
            break;
        case M_TRILL: case M_TAP:
            o->F[0] = 380;
            if (p == P_ALVEOLAR || p == P_DENTAL) { o->F[1] = 1450; o->F[2] = 2500; }
            if (p == P_UVULAR) { o->F[0] = 450; o->F[1] = 1150; o->F[2] = 2400; }
            if (p == P_RETROFLEX) { o->F[1] = 1400; o->F[2] = 1800; }
            break;
        case M_FRIC: case M_LATFRIC:
            if (p == P_LABVELAR) { o->F[0] = 290; o->F[1] = 650; o->F[2] = 2200; }
            if (s->voiced) o->F[0] = (p == P_PHARYNGEAL || p == P_EPIGLOTTAL || p == P_UVULAR) ? L[0] : 260;
            break;
        }
        if (s->place_shift != 0) o->F[1] += 150 * s->place_shift;
    }
    /* secondary articulations */
    if (s->labialized) { o->F[1] = lerp(o->F[1], 700, 0.45f); o->F[2] -= 150; }
    if (s->palatalized) { o->F[1] = lerp(o->F[1], 2150, 0.6f); o->F[2] = lerp(o->F[2], 2950, 0.5f); }
    if (s->velarized) { o->F[1] = lerp(o->F[1], 1000, 0.55f); }
    if (s->pharyngealized) { o->F[0] = o->F[0] < 600 ? lerp(o->F[0], 650, 0.6f) : o->F[0]; o->F[1] = lerp(o->F[1], 1050, 0.5f); }
    if (s->kind == K_CONS && s->rhotic > 0) { o->F[2] = lerp(o->F[2], 1700, s->rhotic); }
}

/* coarticulation: blend consonant locus toward a vowel */
static void coartic(const Seg *c, const Formants *own, const Formants *v, Formants *o) {
    float k;
    *o = *own;
    if (c->kind != K_CONS) return;
    if (is_obstruent(c)) k = LOCUS[c->place][3];
    else if (c->manner == M_NASAL) k = (c->place == P_VELAR) ? 0.6f : 0.3f;
    else if (c->manner == M_TRILL || c->manner == M_TAP) k = 0.35f;
    else k = 0.12f; /* approximants keep their own target */
    if (c->manner == M_FRIC && c->place == P_GLOTTAL) k = 1.0f;
    float k1 = is_obstruent(c) ? 0.25f : k * 0.5f;
    if (c->place == P_UVULAR || c->place == P_PHARYNGEAL || c->place == P_EPIGLOTTAL) k1 = 0.3f;
    if (c->place == P_GLOTTAL) k1 = 1.0f;
    o->F[0] = own->F[0] + k1 * (v->F[0] - own->F[0]);
    o->F[1] = own->F[1] + k * (v->F[1] - own->F[1]);
    o->F[2] = own->F[2] + k * (v->F[2] - own->F[2]);
    o->F[3] = own->F[3] + 0.8f * (v->F[3] - own->F[3]);
    o->F[4] = v->F[4];
    if (c->place == P_GLOTTAL) for (int i = 0; i < 5; i++) o->B[i] = v->B[i] * 1.3f;
}

/* ------------------------------------------------------------------ */
/* Segment expansion (prenasalization, aspiration, releases)           */
/* ------------------------------------------------------------------ */

static int seg_append(SegList *l, const Seg *s) {
    if (l->n == l->cap) {
        int nc = l->cap ? l->cap * 2 : 64;
        Seg *p = realloc(l->s, (size_t)nc * sizeof *p);
        if (!p) return -1;
        l->s = p; l->cap = nc;
    }
    l->s[l->n++] = *s;
    return 0;
}

static Seg mk_cons(int place, int manner, int voiced, float len) {
    Seg s; memset(&s, 0, sizeof s);
    s.kind = K_CONS; s.place = place; s.manner = manner; s.voiced = voiced; s.length = len;
    s.syllabic = -1; s.short_release = 1;
    return s;
}

static int expand(const SegList *in, SegList *out) {
    memset(out, 0, sizeof *out);
    for (int i = 0; i < in->n; i++) {
        Seg s = in->s[i];
        if (s.prenasal && s.kind == K_CONS) {
            int pl = s.place;
            if (pl == P_LABVELAR) pl = P_VELAR;
            Seg n = mk_cons(pl, M_NASAL, 1, 0.6f);
            n.word_start = s.word_start; n.stress = s.stress; n.sym = 'n';
            s.word_start = 0; s.stress = 0;
            if (seg_append(out, &n)) return -1;
        }
        if (s.preaspirated) {
            Seg h = mk_cons(P_GLOTTAL, M_FRIC, 0, 0.6f);
            h.sym = 'h';
            if (seg_append(out, &h)) return -1;
        }
        if (s.kind == K_CONS && s.voiceless_mod && (s.manner == M_NASAL || s.manner == M_APPROX ||
            s.manner == M_LATAPPROX || s.manner == M_TRILL || s.manner == M_TAP)) {
            s.voiced = 0;
        }
        if (seg_append(out, &s)) return -1;
        if (s.kind == K_CONS && (s.nasal_release || s.lateral_release)) {
            Seg r = mk_cons(s.place == P_LABVELAR ? P_BILABIAL : s.place,
                            s.nasal_release ? M_NASAL : M_LATAPPROX, 1, 0.7f);
            r.sym = s.nasal_release ? 'n' : 'l';
            if (seg_append(out, &r)) return -1;
        }
        if (s.aspirated && !(s.kind == K_CONS && (s.manner == M_PLOSIVE || s.manner == M_AFFRICATE ||
                                                 s.manner == M_CLICK || s.manner == M_IMPLOSIVE))) {
            Seg h = mk_cons(P_GLOTTAL, M_FRIC, s.aspirated == 2, 0.5f);
            h.sym = 'h';
            if (seg_append(out, &h)) return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Durations                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    float closure, burst, vot, gap; /* stop phases (ms) */
} StopTiming;

static void stop_timing(const Seg *s, StopTiming *t) {
    memset(t, 0, sizeof *t);
    int pl = s->place;
    t->burst = (pl == P_BILABIAL || pl == P_LABIODENTAL) ? 6 : (pl == P_VELAR || pl == P_UVULAR || pl == P_PALATAL) ? 14 : 9;
    if (s->manner == M_AFFRICATE) t->burst = 4;
    if (s->manner == M_CLICK) t->burst = s->fric_lateral || pl == P_DENTAL ? 18 : 6;
    if (s->manner == M_IMPLOSIVE) t->burst = 4;
    if (s->aspirated) t->vot = 60;
    else if (s->voiced) t->vot = 0;
    else t->vot = 14;
    if (s->manner == M_AFFRICATE) {
        float f = s->fric_voiced ? 45 : 60;
        t->burst += f;
        if (!s->aspirated) t->vot = s->voiced ? 0 : 6;
    }
    if (s->ejective) { t->vot = 0; t->gap = 45; t->burst *= 1.2f; }
    if (s->unreleased || s->nasal_release || s->lateral_release) { t->burst = 0; t->vot = 0; }
    if (s->place == P_GLOTTAL || s->place == P_EPIGLOTTAL) { if (s->place == P_GLOTTAL) t->burst = 0; t->vot = 0; }
}

static float base_duration(const Seg *s) {
    if (s->kind == K_VOWEL) {
        float d = 72 + 32 * (1 - clampf(s->height, 0, 1));
        if (s->syllabic == -1) d *= 0.55f;
        return d;
    }
    switch (s->manner) {
    case M_PLOSIVE: return s->place == P_GLOTTAL ? 55 : (s->voiced ? 50 : 60);
    case M_AFFRICATE: return s->voiced ? 45 : 50;
    case M_IMPLOSIVE: return 55;
    case M_CLICK: return 45;
    case M_NASAL: return 62;
    case M_TRILL: return s->place == P_ALVEOLAR ? 75 : 65;
    case M_TAP: case M_LATTAP: return 24;
    case M_LATAPPROX: return 58;
    case M_APPROX:
        if (s->place == P_ALVEOLAR || s->place == P_RETROFLEX) return 58;
        return 48;
    case M_FRIC: case M_LATFRIC:
        if (s->place == P_GLOTTAL) return 58;
        if (s->sibilant) return s->voiced ? 78 : 98;
        return s->voiced ? 68 : 88;
    }
    return 60;
}

/* ------------------------------------------------------------------ */
/* Anchors (formant keypoints)                                         */
/* ------------------------------------------------------------------ */

typedef struct { float t; Formants f; float fnz, nas; } Anchor;
typedef struct { Anchor *a; int n, cap; } Anchors;

static int add_anchor(Anchors *A, float t, const Formants *f, float nas) {
    if (A->n == A->cap) {
        int nc = A->cap ? A->cap * 2 : 128;
        Anchor *p = realloc(A->a, (size_t)nc * sizeof *p);
        if (!p) return -1;
        A->a = p; A->cap = nc;
    }
    if (A->n && t < A->a[A->n - 1].t) t = A->a[A->n - 1].t;
    A->a[A->n].t = t; A->a[A->n].f = *f; A->a[A->n].nas = nas; A->a[A->n].fnz = 0;
    A->n++;
    return 0;
}

/* transition time (ms) a vowel spends moving from/to consonant c */
static float trans_ms(const Seg *c) {
    if (!c || c->kind == K_PAUSE) return 0;
    if (c->kind == K_VOWEL) return 0;
    switch (c->manner) {
    case M_PLOSIVE: case M_IMPLOSIVE: case M_CLICK: return c->aspirated ? 20 : 40;
    case M_AFFRICATE: return 30;
    case M_NASAL: return 30;
    case M_TRILL: case M_TAP: case M_LATTAP: return 30;
    case M_LATAPPROX: return 35;
    case M_APPROX: return 60;
    case M_FRIC: case M_LATFRIC: return c->place == P_GLOTTAL ? 0 : 35;
    }
    return 35;
}

/* ------------------------------------------------------------------ */
/* Plan                                                                */
/* ------------------------------------------------------------------ */

typedef struct { float t, f0; } PitchPt;
typedef struct { PitchPt *p; int n, cap; } PitchPts;

static int add_pitch(PitchPts *P, float t, float f0) {
    if (P->n == P->cap) {
        int nc = P->cap ? P->cap * 2 : 128;
        PitchPt *q = realloc(P->p, (size_t)nc * sizeof *q);
        if (!q) return -1;
        P->p = q; P->cap = nc;
    }
    P->p[P->n].t = t; P->p[P->n].f0 = f0; P->n++;
    return 0;
}

static void set_spec(Frame *f, const Spec *sp, float amp, float fscale, float shift) {
    for (int k = 0; k < NPAR; k++) {
        f->pf[k] = sp->f[k] * fscale * shift;
        f->pb[k] = sp->bw[k] * fscale;
        f->pa[k] = db2lin(sp->db[k]);
    }
    f->bypass = db2lin(sp->bypass_db);
    f->af = amp * db2lin(sp->level_db);
}

static int cmp_pitch(const void *a, const void *b) {
    float x = ((const PitchPt *)a)->t, y = ((const PitchPt *)b)->t;
    return (x > y) - (x < y);
}

static void smooth(float *x, int n, size_t stride, int w) {
    if (w < 2 || n < 3) return;
    float *tmp = malloc((size_t)n * sizeof *tmp);
    if (!tmp) return;
    for (int i = 0; i < n; i++) tmp[i] = *(float *)((char *)x + (size_t)i * stride);
    int h = w / 2;
    float acc = 0; int cnt = 0;
    /* centered moving average via prefix sums */
    float *ps = malloc((size_t)(n + 1) * sizeof *ps);
    if (!ps) { free(tmp); return; }
    ps[0] = 0;
    for (int i = 0; i < n; i++) ps[i + 1] = ps[i] + tmp[i];
    for (int i = 0; i < n; i++) {
        int a = i - h, b = i + h + 1;
        if (a < 0) a = 0;
        if (b > n) b = n;
        acc = ps[b] - ps[a]; cnt = b - a;
        *(float *)((char *)x + (size_t)i * stride) = acc / cnt;
    }
    free(ps); free(tmp);
}

#define SMOOTH_FIELD(fr, n, field, w) smooth(&(fr)[0].field, n, sizeof(Frame), w)

int ipr_plan(SegList *in, const ipr_voice *v, const ipr_options *o, Plan *out) {
    SegList L;
    memset(out, 0, sizeof *out);
    if (expand(in, &L)) { ipr_seglist_free(&L); return IPR_ERR_MEMORY; }
    Seg *S = L.s;
    int N = L.n;
    float speed = v->speed * (o ? o->speed : 1.0f);
    if (speed < 0.1f) speed = 0.1f;
    float fscale = v->formant_scale;

    /* ---- syllables: nucleus flags & syllable ids ---- */
    int syl = -1;
    for (int i = 0; i < N; i++) S[i].nucleus = is_nucleus(&S[i]);
    /* words with no nucleus: make the most sonorous voiced consonant a nucleus */
    for (int i = 0; i < N;) {
        int j = i;
        while (j < N && S[j].kind != K_PAUSE) j++;
        int has = 0, best = -1, bestscore = -1;
        for (int k = i; k < j; k++) {
            if (S[k].nucleus) has = 1;
            int sc = 0;
            if (S[k].kind == K_CONS) {
                if (S[k].manner == M_APPROX || S[k].manner == M_LATAPPROX) sc = 4;
                else if (S[k].manner == M_NASAL) sc = 3;
                else if (S[k].manner == M_TRILL) sc = 2;
                else if (S[k].voiced) sc = 1;
            }
            if (sc > bestscore) { bestscore = sc; best = k; }
        }
        if (!has && best >= 0 && bestscore > 0) S[best].nucleus = 1;
        i = j + 1;
    }
    /* assign syllables: onset consonants go to the following nucleus */
    {
        int i = 0;
        while (i < N) {
            if (S[i].kind == K_PAUSE) { S[i].syl = -1; i++; continue; }
            /* word span */
            int j = i;
            while (j < N && S[j].kind != K_PAUSE) j++;
            int cur = -1;
            int k = i;
            while (k < j) {
                /* consonant run until next nucleus */
                int r = k;
                while (r < j && !S[r].nucleus) r++;
                if (r == j) { /* coda run at word end */
                    for (int q = k; q < j; q++) S[q].syl = cur >= 0 ? cur : ++syl;
                    if (cur < 0) cur = syl;
                    break;
                }
                /* split run [k, r): explicit boundary (stress/'.') wins, else last consonant is onset */
                int split = (cur < 0 || r == k) ? k : r - 1;
                if (cur >= 0 && r - k >= 1) {
                    for (int q = k; q < r; q++) if (S[q].stress != 0) { split = q; break; }
                    if (S[r].stress != 0 && split == r - 1) {
                        int expl = 0;
                        for (int q = k; q < r; q++) if (S[q].stress != 0) expl = 1;
                        if (!expl) split = r; /* mark sits on nucleus itself */
                    }
                    /* keep common onset clusters (obstruent + liquid/glide) together */
                    if (split == r - 1 && r - 2 >= k) {
                        Seg *a = &S[r - 2], *b = &S[r - 1];
                        int explicit_b = 0;
                        for (int q = k; q < r; q++) if (S[q].stress != 0) explicit_b = 1;
                        if (!explicit_b && is_obstruent(a) && is_sonorant_cons(b) && b->manner != M_NASAL) split = r - 2;
                    }
                    if (r == k) split = r;
                }
                if (cur < 0) split = k;
                for (int q = k; q < split; q++) S[q].syl = cur;
                ++syl;
                for (int q = split; q <= r; q++) S[q].syl = syl;
                cur = syl;
                k = r + 1;
                /* a non-syllabic vowel right after the nucleus belongs to it */
            }
            i = j;
        }
    }
    int nsyl = syl + 1;
    int *syl_stress = calloc((size_t)(nsyl > 0 ? nsyl : 1), sizeof(int));
    int *syl_nuc = malloc((size_t)(nsyl > 0 ? nsyl : 1) * sizeof(int));
    if (!syl_stress || !syl_nuc) { free(syl_stress); free(syl_nuc); ipr_seglist_free(&L); return IPR_ERR_MEMORY; }
    for (int k = 0; k < nsyl; k++) syl_nuc[k] = -1;
    for (int i = 0; i < N; i++) {
        if (S[i].syl < 0 || S[i].kind == K_PAUSE) continue;
        if (S[i].stress > syl_stress[S[i].syl]) syl_stress[S[i].syl] = S[i].stress;
        if (S[i].nucleus && syl_nuc[S[i].syl] < 0) syl_nuc[S[i].syl] = i;
    }
    /* move tones from non-nucleus segments to the nucleus */
    for (int i = 0; i < N; i++) {
        if (S[i].kind == K_PAUSE || S[i].syl < 0 || S[i].nucleus || !S[i].ntone) continue;
        int nu = syl_nuc[S[i].syl];
        if (nu >= 0 && S[nu].ntone == 0) {
            S[nu].ntone = S[i].ntone;
            memcpy(S[nu].tone, S[i].tone, sizeof S[i].tone);
            S[i].ntone = 0;
        }
    }

    /* ---- durations ---- */
    float lead = (o && (o->flags & IPR_FLAG_NO_LEADING_SILENCE)) ? 0 : 40;
    float t = lead;
    for (int i = 0; i < N; i++) {
        Seg *s = &S[i];
        if (s->kind == K_PAUSE) {
            float d = 0;
            switch (s->pause_kind) {
            case PZ_WORD: d = o ? o->word_gap_ms : 0; break;
            case PZ_SHORT: d = 100; break;
            case PZ_COMMA: d = 230; break;
            case PZ_MINOR: d = 260; break;
            case PZ_MAJOR: d = 420; break;
            default: d = 480; break;
            }
            if (s->pause_kind != PZ_WORD) d /= speed > 1 ? sqrtf(speed) : speed;
            s->dur = d;
            s->t0 = t; t += d;
            continue;
        }
        float d = base_duration(s);
        int st = (s->syl >= 0) ? syl_stress[s->syl] : 0;
        if (st < 0) st = 0;
        /* does the word contain any stress? */
        int word_has_stress = 0;
        {
            int a = i, b = i;
            while (a > 0 && S[a - 1].kind != K_PAUSE) a--;
            while (b < N - 1 && S[b + 1].kind != K_PAUSE) b++;
            for (int q = a; q <= b; q++) if (S[q].stress > 0) word_has_stress = 1;
        }
        /* phrase-final syllable lengthening */
        int final_syl = 0;
        {
            int b = i;
            while (b < N - 1 && S[b + 1].kind != K_PAUSE) b++;
            int next_pause = b + 1 < N ? S[b + 1].pause_kind : PZ_STATEMENT;
            if (b + 1 >= N || next_pause != PZ_WORD) final_syl = (S[b].syl == s->syl);
        }
        if (s->kind == K_VOWEL || s->nucleus) {
            if (st == 2) d *= 1.4f; else if (st == 1) d *= 1.15f; else if (word_has_stress) d *= 0.85f;
            if (final_syl) d *= (s->length > 1.4f) ? 1.1f : 1.3f;
            if (s->nasal > 0) d *= 1.05f;
            if (s->kind == K_CONS && s->nucleus) d *= 1.5f;
        } else {
            int prevc = i > 0 && S[i - 1].kind == K_CONS && !S[i - 1].nucleus;
            int nextc = i + 1 < N && S[i + 1].kind == K_CONS && !S[i + 1].nucleus;
            if (prevc || nextc) d *= 0.82f;
            if (final_syl) d *= 1.15f;
            if (st == 2) d *= 1.08f;
        }
        d *= s->length;
        if (s->short_release) d = base_duration(s) * s->length * 0.8f;
        d /= speed;
        if (is_stoplike(s)) {
            StopTiming tm; stop_timing(s, &tm);
            float rel = (tm.burst + tm.vot + tm.gap) / (speed > 1 ? sqrtf(speed) : speed);
            if (i == 0 || S[i - 1].kind == K_PAUSE) d = (d > 35 ? 35 : d); /* initial closure is silence */
            d += rel;
        }
        if (d < 8) d = 8;
        s->dur = d;
        s->t0 = t;
        t += d;
    }
    float total = t + lead;
    int nf = (int)ceilf(total) + 2;
    Frame *F = calloc((size_t)nf, sizeof *F);
    if (!F) { free(syl_stress); free(syl_nuc); ipr_seglist_free(&L); return IPR_ERR_MEMORY; }

    /* defaults */
    for (int i = 0; i < nf; i++) {
        Frame *f = &F[i];
        f->oq = v->open_quotient;
        f->tilt = v->spectral_tilt;
        f->breath = v->breathiness;
        f->fnp = 270; f->bnp = 100; f->fnz = 270; f->bnz = 100;
        for (int k = 0; k < NPAR; k++) { f->pf[k] = 1000; f->pb[k] = 500; f->pa[k] = 0; }
    }

    /* ---- formant anchors ---- */
    Anchors A = {0};
    Formants schwa; vowel_formants(0.5f, 0.5f, 0, 0, &schwa);
    Formants *own = malloc((size_t)(N > 0 ? N : 1) * sizeof *own);
    if (!own) goto oom;
    for (int i = 0; i < N; i++) if (S[i].kind != K_PAUSE) seg_target(&S[i], &own[i]);

    for (int i = 0; i < N; i++) {
        Seg *s = &S[i];
        if (s->kind == K_PAUSE) continue;
        float t0 = s->t0, t1 = s->t0 + s->dur;
        Seg *pv = (i > 0 && S[i - 1].kind != K_PAUSE) ? &S[i - 1] : NULL;
        Seg *nx = (i + 1 < N && S[i + 1].kind != K_PAUSE) ? &S[i + 1] : NULL;
        float nas = (s->kind == K_VOWEL) ? s->nasal : 0;
        if (s->kind == K_VOWEL) {
            float ti = trans_ms(pv), to = trans_ms(nx);
            if (pv && pv->kind == K_VOWEL) ti = s->dur * (s->syllabic == -1 ? 0.6f : 0.3f);
            if (nx && nx->kind == K_VOWEL) to = s->dur * (s->syllabic == -1 ? 0.3f : 0.3f);
            if (s->syllabic == -1) { /* glide vowel: single target in the middle */
                float tm = t0 + s->dur * 0.6f;
                if (add_anchor(&A, tm, &own[i], nas)) goto oom;
                continue;
            }
            ti = fminf(ti, s->dur * 0.45f); to = fminf(to, s->dur * 0.45f);
            if (add_anchor(&A, t0 + ti, &own[i], nas)) goto oom;
            if (add_anchor(&A, t1 - to, &own[i], nas)) goto oom;
            continue;
        }
        /* consonant: find neighbouring vowels for coarticulation */
        int pvI = -1, nxI = -1;
        for (int k = i - 1; k >= 0 && k >= i - 3 && S[k].kind != K_PAUSE; k--) if (S[k].kind == K_VOWEL) { pvI = k; break; }
        for (int k = i + 1; k < N && k <= i + 3 && S[k].kind != K_PAUSE; k++) if (S[k].kind == K_VOWEL) { nxI = k; break; }
        const Formants *vp = pvI >= 0 ? &own[pvI] : (nxI >= 0 ? &own[nxI] : &schwa);
        const Formants *vn = nxI >= 0 ? &own[nxI] : (pvI >= 0 ? &own[pvI] : &schwa);
        Formants fa, fb;
        coartic(s, &own[i], vp, &fa);
        coartic(s, &own[i], vn, &fb);
        if (s->place == P_GLOTTAL && s->kind == K_CONS && !s->labialized && !s->palatalized) {
            /* h, ɦ, ʔ: vocal tract keeps the neighbouring vowel shape; no anchors */
            if (!pv && !nx) { if (add_anchor(&A, t0, &fb, 0)) goto oom; }
            else if (!pv || pv->kind != K_VOWEL) { if (add_anchor(&A, t0 + s->dur * 0.3f, &fb, 0)) goto oom; }
            continue;
        }
        if (is_stoplike(s)) {
            StopTiming tm; stop_timing(s, &tm);
            float rel = (tm.burst + tm.vot + tm.gap) / (speed > 1 ? sqrtf(speed) : speed);
            float trel = t1 - rel;
            if (add_anchor(&A, t0, &fa, 0)) goto oom;
            if (add_anchor(&A, trel, &fb, 0)) goto oom;
            if (s->manner == M_AFFRICATE) {
                /* during the frication the tract moves toward the fricative place */
                Seg fr = *s; fr.place = s->fric_place; fr.manner = M_FRIC;
                Formants ff, fc; seg_target(&fr, &ff); coartic(&fr, &ff, vn, &fc);
                if (add_anchor(&A, trel + tm.burst * 0.6f, &fc, 0)) goto oom;
            }
            continue;
        }
        if (s->manner == M_APPROX || s->manner == M_LATAPPROX || s->manner == M_NASAL) {
            float a = s->dur * 0.25f;
            if (add_anchor(&A, t0 + a, &fa, 0)) goto oom;
            if (add_anchor(&A, t1 - a, &fb, 0)) goto oom;
            continue;
        }
        if (add_anchor(&A, t0, &fa, 0)) goto oom;
        if (add_anchor(&A, t1, &fb, 0)) goto oom;
    }
    if (A.n == 0) { if (add_anchor(&A, 0, &schwa, 0)) goto oom; }

    /* interpolate anchors into frames */
    {
        int ai = 0;
        for (int fi = 0; fi < nf; fi++) {
            float tt = (float)fi;
            while (ai + 1 < A.n && A.a[ai + 1].t <= tt) ai++;
            const Anchor *a = &A.a[ai];
            const Anchor *b = (ai + 1 < A.n) ? &A.a[ai + 1] : a;
            float u = 0;
            if (tt <= a->t) u = 0;
            else if (b->t > a->t) u = clampf((tt - a->t) / (b->t - a->t), 0, 1);
            for (int k = 0; k < 5; k++) {
                F[fi].F[k] = lerp(a->f.F[k], b->f.F[k], u);
                F[fi].B[k] = lerp(a->f.B[k], b->f.B[k], u);
            }
            float nas = lerp(a->nas, b->nas, u);
            F[fi].fnz = 270 + 150 * nas;
            F[fi].B[0] *= 1 + 0.6f * nas;
        }
    }

    /* ---- source parameters per segment ---- */
    for (int i = 0; i < N; i++) {
        Seg *s = &S[i];
        if (s->kind == K_PAUSE) continue;
        int f0i = (int)floorf(s->t0), f1i = (int)floorf(s->t0 + s->dur);
        if (f1i > nf) f1i = nf;
        int len = f1i - f0i;
        Seg *nx = (i + 1 < N && S[i + 1].kind != K_PAUSE) ? &S[i + 1] : NULL;
        int nxI = -1;
        for (int k = i + 1; k < N && k <= i + 3 && S[k].kind != K_PAUSE; k++) if (S[k].kind == K_VOWEL) { nxI = k; break; }
        if (nxI < 0) for (int k = i - 1; k >= 0 && k >= i - 3 && S[k].kind != K_PAUSE; k--) if (S[k].kind == K_VOWEL) { nxI = k; break; }
        float vF2 = nxI >= 0 ? own[nxI].F[1] : 1500;

        for (int fi = f0i; fi < f1i; fi++) {
            Frame *f = &F[fi];
            float pos = (float)(fi - f0i);   /* ms into segment */
            f->breath = v->breathiness;
            if (s->breathy) { f->breath = fmaxf(f->breath, 0.55f); f->oq = fmaxf(f->oq, 0.8f); f->tilt = fmaxf(f->tilt, 0.6f); }
            if (s->creaky) f->creak = 1.0f;
            if (s->glottalized) f->creak = fmaxf(f->creak, 0.7f);

            if (s->kind == K_VOWEL) {
                if (s->voiced) { f->av = (1.0f + 0.5f * clampf(s->height, 0, 1)) * (1.0f + 0.7f * s->nasal); }
                else { f->av = 0; f->ah = 0.55f; }
                continue;
            }
            int p = s->place;
            switch (s->manner) {
            case M_NASAL: {
                f->av = s->voiced ? 0.8f : 0; if (!s->voiced) f->ah = 0.35f;
                f->fnp = 270; f->bnp = 100;
                f->fnz = NASAL_ZERO[p]; f->bnz = 150;
                break;
            }
            case M_APPROX:
                f->av = s->voiced ? 0.8f : 0;
                if (!s->voiced) { f->ah = 0.45f; set_spec(f, &FRIC_SPEC[p], 0.6f * v->frication_gain, 1, 1); }
                break;
            case M_LATAPPROX:
                f->av = s->voiced ? 1.0f : 0;
                if (!s->voiced) { set_spec(f, &LATFRIC_SPEC, 0.6f * v->frication_gain, 1, 1); f->ah = 0.2f; }
                break;
            case M_TAP: case M_LATTAP: {
                float mid = len * 0.5f;
                int closed = fabsf(pos - mid) < fminf(7.0f, len * 0.3f);
                f->av = s->voiced ? (closed ? 0.25f : 0.8f) : 0;
                if (!s->voiced) f->ah = closed ? 0 : 0.35f;
                break;
            }
            case M_TRILL: {
                float period = (p == P_UVULAR) ? 30.0f : 32.0f;
                float ph = fmodf(pos + period * 0.5f, period) / period;
                int closed = ph < 0.4f;
                float depth = (p == P_UVULAR) ? 0.45f : 0.22f;
                f->av = s->voiced ? (closed ? depth : 0.85f) : 0;
                if (!s->voiced) f->ah = closed ? 0.1f : 0.4f;
                if (p == P_UVULAR) set_spec(f, &FRIC_SPEC[P_UVULAR], 0.25f * v->frication_gain, 1, 1);
                break;
            }
            case M_FRIC: case M_LATFRIC: {
                const Spec *sp = s->manner == M_LATFRIC ? &LATFRIC_SPEC : &FRIC_SPEC[p];
                float shift = 1;
                if (p == P_VELAR) shift = clampf(vF2 * 1.1f, 1200, 2900) / 1800.0f;
                float amp = v->frication_gain;
                if (!s->sibilant && s->manner == M_FRIC && (p == P_ALVEOLAR || p == P_POSTALV || p == P_RETROFLEX || p == P_ALVPAL)) amp *= 0.35f;
                /* onset/offset ramps */
                float e = fminf(fminf(pos, (float)len - pos) / 12.0f, 1.0f);
                if (p == P_GLOTTAL) {
                    f->av = s->voiced ? 0.45f : 0;
                    f->ah = s->voiced ? 0.35f : 0.6f * e;
                    if (s->voiced) { f->breath = 0.8f; f->oq = 0.8f; }
                    break;
                }
                set_spec(f, sp, amp * (s->voiced ? 0.55f : 1.0f) * (0.3f + 0.7f * e), fscale > 1 ? sqrtf(fscale) : 1, shift);
                if (s->voiced) { f->av = 0.5f; }
                if (p == P_PHARYNGEAL || p == P_EPIGLOTTAL) { f->ah = s->voiced ? 0.15f : 0.45f; if (s->voiced) f->creak = fmaxf(f->creak, 0.35f); f->av = s->voiced ? 0.65f : 0; }
                if (p == P_LABVELAR) f->ah = 0.4f;
                if (p == P_UVULAR && s->voiced) f->av = 0.6f;
                break;
            }
            default: { /* stop-like */
                StopTiming tm; stop_timing(s, &tm);
                float sc = speed > 1 ? sqrtf(speed) : speed;
                float burst = tm.burst / sc, vot = tm.vot / sc, gap = tm.gap / sc;
                float rel = burst + vot + gap;
                float closure = len - rel;
                if (pos < closure) {
                    /* closure */
                    if (s->manner == M_IMPLOSIVE) { f->av = 0.25f + 0.4f * pos / fmaxf(closure, 1); f->tilt = 0.7f; }
                    else if (s->voiced && p != P_GLOTTAL) { f->av = 0.2f; f->tilt = 0.92f; }
                    else if (s->prenasal && pos < 5) { f->av = 0.3f; }
                    if (p == P_GLOTTAL) { f->av = 0; }
                    break;
                }
                float rp = pos - closure;
                if (s->manner == M_CLICK) {
                    if (rp < 1.0f) {
                        float amp = 1.0f;
                        const Spec *sp;
                        static const Spec CK_BIL = {{800, 2000, 0, 0}, {400, 800, 1, 1}, {0, -8, NONE, NONE}, -12, -6};
                        static const Spec CK_DEN = {{4000, 6500, 2500, 0}, {1500, 2000, 800, 1}, {0, -3, -8, NONE}, -10, -6};
                        static const Spec CK_ALV = {{1800, 3000, 0, 0}, {350, 800, 1, 1}, {0, -8, NONE, NONE}, -20, 4};
                        static const Spec CK_PAL = {{3000, 4500, 0, 0}, {400, 900, 1, 1}, {0, -6, NONE, NONE}, -20, 2};
                        static const Spec CK_LAT = {{2500, 4000, 0, 0}, {800, 1200, 1, 1}, {0, -4, NONE, NONE}, -14, -6};
                        sp = p == P_BILABIAL ? &CK_BIL : p == P_DENTAL ? &CK_DEN : p == P_PALATAL ? &CK_PAL :
                             s->fric_lateral ? &CK_LAT : &CK_ALV;
                        set_spec(f, sp, 0, 1, 1);
                        f->imp = amp * db2lin(sp->level_db) * 3.0f;
                        f->af = 0;
                    } else if (rp < burst) {
                        const Spec *sp = s->fric_lateral ? &LATFRIC_SPEC : &FRIC_SPEC[p == P_DENTAL ? P_DENTAL : p];
                        float noisy = (s->fric_lateral || p == P_DENTAL) ? 1.0f : 0.25f;
                        set_spec(f, sp, noisy * 1.0f * v->frication_gain * (1 - rp / burst), 1, 1);
                    } else {
                        f->av = 0;
                    }
                    if (s->voiced) f->av = 0.5f;
                    break;
                }
                if (rp < burst) {
                    if (s->manner == M_AFFRICATE) {
                        int fp = s->fric_place;
                        const Spec *sp = s->fric_lateral ? &LATFRIC_SPEC : &FRIC_SPEC[fp];
                        float a = v->frication_gain * (s->fric_voiced ? 0.6f : 1.0f);
                        if (!s->fric_sibilant && !s->fric_lateral && (fp == P_ALVEOLAR || fp == P_POSTALV)) a *= 0.35f;
                        float e = rp < 3 ? 1.6f : fminf(1.0f, (burst - rp) / 10.0f);
                        set_spec(f, sp, a * e, fscale > 1 ? sqrtf(fscale) : 1, 1);
                        if (s->fric_voiced) f->av = 0.5f;
                        if (s->ejective) f->af *= 1.2f;
                    } else if (p != P_GLOTTAL) {
                        float shift = 1;
                        if (p == P_VELAR) shift = clampf(vF2 * 1.15f, 1300, 3000) / 2000.0f;
                        float decay = expf(-rp / fmaxf(burst * 0.45f, 1));
                        float a = v->frication_gain * (s->ejective ? 1.5f : 1.0f) * (s->voiced ? 0.6f : 1.0f);
                        if (s->manner == M_IMPLOSIVE) a *= 0.3f;
                        set_spec(f, &BURST_SPEC[p], a * decay, fscale > 1 ? sqrtf(fscale) : 1, shift);
                        f->ah = (s->voiced ? 0.0f : 0.15f) * decay;
                        if (s->voiced) f->av = 0.6f;
                    }
                    break;
                }
                rp -= burst;
                if (rp < gap) { f->av = 0; break; } /* ejective glottal hold */
                rp -= gap;
                if (rp < vot) {
                    f->ah = s->aspirated ? 0.5f * (1.0f - 0.4f * rp / vot) : 0.3f;
                    if (s->aspirated == 2) { f->av = 0.5f; f->breath = 0.9f; f->oq = 0.85f; }
                    break;
                }
                break;
            }
            }
        }
        /* overlays onto neighbours */
        if (s->kind == K_CONS && (s->place == P_GLOTTAL && s->manner == M_PLOSIVE)) {
            for (int fi = f0i - 25; fi < f0i; fi++) if (fi >= 0) F[fi].creak = fmaxf(F[fi].creak, 0.6f * (fi - f0i + 25) / 25.0f);
            for (int fi = f1i; fi < f1i + 25 && fi < nf; fi++) F[fi].creak = fmaxf(F[fi].creak, 0.6f * (f1i + 25 - fi) / 25.0f);
        }
        if (s->kind == K_CONS && s->ejective) {
            for (int fi = f1i; fi < f1i + 20 && fi < nf; fi++) F[fi].creak = fmaxf(F[fi].creak, 0.6f);
        }
        if (s->kind == K_CONS && s->manner == M_NASAL && s->voiced) {
            /* anticipatory/carryover nasalization of neighbouring vowels */
            for (int fi = f0i - 35; fi < f0i; fi++) if (fi >= 0 && F[fi].av > 0.9f) F[fi].fnz = fmaxf(F[fi].fnz, 270 + 120 * (fi - f0i + 35) / 35.0f);
            for (int fi = f1i; fi < f1i + 20 && fi < nf; fi++) if (F[fi].av > 0.9f) F[fi].fnz = fmaxf(F[fi].fnz, 270 + 120 * (f1i + 20 - fi) / 20.0f);
        }
        (void)nx;
    }

    /* ---- pitch ---- */
    PitchPts P = {0};
    float base = v->pitch * powf(2.0f, (o ? o->pitch_shift : 0) / 12.0f);
    float R = v->pitch_range;
    int intonation = !(o && (o->flags & IPR_FLAG_NO_INTONATION));
    {
        int i = 0;
        while (i < N) {
            /* phrase: up to a non-word pause */
            int j = i;
            while (j < N && !(S[j].kind == K_PAUSE && S[j].pause_kind != PZ_WORD)) j++;
            int ptype = (j < N) ? S[j].pause_kind : PZ_STATEMENT;
            /* nuclei of the phrase */
            int nn = 0, tonal = 0, last_stressed = -1, any_stress = 0;
            for (int k = i; k < j; k++) if (S[k].kind != K_PAUSE && S[k].nucleus && syl_nuc[S[k].syl] == k) {
                nn++;
                if (S[k].ntone) tonal = 1;
                if (syl_stress[S[k].syl] > 0) { last_stressed = k; any_stress = 1; }
            }
            float tstart = (i < N) ? S[i].t0 : 0;
            float tend = (j > i) ? S[j - 1].t0 + S[j - 1].dur : tstart + 1;
            float plen = fmaxf(tend - tstart, 1);
            float step = 0;
            int idx = 0;
            int last_nuc = -1;
            for (int k = i; k < j; k++) if (S[k].kind != K_PAUSE && S[k].nucleus && syl_nuc[S[k].syl] == k) last_nuc = k;
            for (int k = i; k < j; k++) {
                Seg *s = &S[k];
                if (s->kind == K_PAUSE || !s->nucleus || syl_nuc[s->syl] != k) continue;
                /* voiced span of the syllable rhyme: nucleus + following voiced segments in the syllable */
                float a = s->t0, b = s->t0 + s->dur;
                for (int q = k + 1; q < j && S[q].kind != K_PAUSE && S[q].syl == s->syl; q++) {
                    if (S[q].voiced && !is_obstruent(&S[q])) b = S[q].t0 + S[q].dur; else break;
                }
                float rel = ((a + b) * 0.5f - tstart) / plen;
                float decl = intonation ? (1.0f - 2.0f * rel) * 1.2f * R : 0;  /* semitones */
                if (s->tone_step) step += 2.2f * s->tone_step;
                int st = syl_stress[s->syl];
                if (tonal) {
                    decl *= 0.5f;
                    if (s->ntone) {
                        int nt = s->ntone;
                        for (int q = 0; q < nt; q++) {
                            float u = nt == 1 ? 0.5f : (float)q / (nt - 1);
                            float tt = lerp(a + (b - a) * 0.1f, b - (b - a) * 0.05f, u);
                            float semi = (s->tone[q] - 3.0f) * 2.6f * R + decl + step;
                            if (nt == 1) {
                                if (add_pitch(&P, a + (b - a) * 0.1f, base * powf(2, semi / 12))) goto oom2;
                                if (add_pitch(&P, b - (b - a) * 0.1f, base * powf(2, semi / 12))) goto oom2;
                                break;
                            }
                            if (k == last_nuc && q == nt - 1 && ptype == PZ_QUESTION && intonation) semi += 2.5f;
                            if (add_pitch(&P, tt, base * powf(2, semi / 12))) goto oom2;
                        }
                    } else {
                        /* toneless syllable in a tone language: neutral, mid-low */
                        float semi = -0.8f * R + decl + step;
                        if (add_pitch(&P, (a + b) * 0.5f, base * powf(2, semi / 12))) goto oom2;
                    }
                } else {
                    float acc = 0;
                    if (st == 2) acc = 3.2f * R; else if (st == 1) acc = 1.4f * R;
                    if (ptype == PZ_EXCLAIM) acc *= 1.4f;
                    if (!intonation) decl = 0;
                    float s0 = decl + acc * 0.5f + step, s1 = decl + acc + step, s2 = decl + acc * 0.6f + step;
                    if (intonation && k == last_nuc) {
                        if (ptype == PZ_QUESTION) { s0 = decl + acc * 0.3f; s1 = s0 + 2.0f * R; s2 = s0 + 6.0f * R; }
                        else if (ptype == PZ_COMMA || ptype == PZ_MINOR) { s2 = s1 + 1.5f * R; }
                        else if (ptype != PZ_WORD) { s2 = decl - 3.5f * R; s1 = fmaxf(s1, s0); }
                    } else if (intonation && k == last_stressed && ptype != PZ_QUESTION && any_stress && nn > 1) {
                        s1 = decl + acc * 1.15f; s2 = decl + acc * 0.2f;
                    }
                    if (add_pitch(&P, a + (b - a) * 0.15f, base * powf(2, s0 / 12))) goto oom2;
                    if (add_pitch(&P, a + (b - a) * 0.5f, base * powf(2, s1 / 12))) goto oom2;
                    if (add_pitch(&P, b - (b - a) * 0.1f, base * powf(2, s2 / 12))) goto oom2;
                }
                idx++;
            }
            i = j + 1;
        }
    }
    if (P.n == 0) { if (add_pitch(&P, 0, base)) goto oom2; }
    qsort(P.p, (size_t)P.n, sizeof *P.p, cmp_pitch);
    {
        int pi = 0;
        for (int fi = 0; fi < nf; fi++) {
            float tt = (float)fi;
            while (pi + 1 < P.n && P.p[pi + 1].t <= tt) pi++;
            const PitchPt *a = &P.p[pi], *b = (pi + 1 < P.n) ? &P.p[pi + 1] : a;
            float u = 0;
            if (tt > a->t && b->t > a->t) u = clampf((tt - a->t) / (b->t - a->t), 0, 1);
            /* interpolate in log domain */
            F[fi].f0 = expf(lerp(logf(a->f0), logf(b->f0), u));
        }
    }

    /* ---- smoothing ---- */
    for (int k = 0; k < 5; k++) { SMOOTH_FIELD(F, nf, F[k], 9); SMOOTH_FIELD(F, nf, B[k], 9); }
    SMOOTH_FIELD(F, nf, fnz, 15);
    SMOOTH_FIELD(F, nf, fnp, 15);
    SMOOTH_FIELD(F, nf, bnz, 9);
    SMOOTH_FIELD(F, nf, f0, 25);
    SMOOTH_FIELD(F, nf, av, 7);
    SMOOTH_FIELD(F, nf, ah, 5);
    SMOOTH_FIELD(F, nf, af, 3);
    SMOOTH_FIELD(F, nf, creak, 9);
    SMOOTH_FIELD(F, nf, breath, 9);
    SMOOTH_FIELD(F, nf, oq, 9);
    SMOOTH_FIELD(F, nf, tilt, 5);

    /* apply vocal tract scaling */
    for (int fi = 0; fi < nf; fi++) {
        Frame *f = &F[fi];
        for (int k = 0; k < 5; k++) {
            f->F[k] *= fscale * v->formant_shift[k];
            f->B[k] *= v->bandwidth_scale * (fscale > 1 ? 1.0f + (fscale - 1) * 0.5f : 1.0f);
        }
        f->fnp *= fscale; f->fnz *= fscale;
    }

    free(P.p);
    free(A.a);
    free(own);
    free(syl_stress); free(syl_nuc);
    ipr_seglist_free(&L);
    out->fr = F; out->n = nf;
    return IPR_OK;
oom2:
    free(P.p);
oom:
    free(A.a); free(own); free(F);
    free(syl_stress); free(syl_nuc);
    ipr_seglist_free(&L);
    return IPR_ERR_MEMORY;
}

void ipr_plan_free(Plan *p) {
    free(p->fr);
    p->fr = NULL; p->n = 0;
}
