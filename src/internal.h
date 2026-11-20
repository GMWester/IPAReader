/* Internal definitions shared between IPAReader source files. */
#ifndef IPR_INTERNAL_H
#define IPR_INTERNAL_H

#include "ipareader.h"
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { K_VOWEL = 0, K_CONS = 1, K_PAUSE = 2 };

enum {
    M_PLOSIVE, M_NASAL, M_TRILL, M_TAP, M_FRIC, M_LATFRIC, M_APPROX,
    M_LATAPPROX, M_LATTAP, M_IMPLOSIVE, M_CLICK, M_AFFRICATE
};

enum {
    P_BILABIAL, P_LABIODENTAL, P_LINGUOLABIAL, P_DENTAL, P_ALVEOLAR,
    P_POSTALV, P_RETROFLEX, P_ALVPAL, P_PALATAL, P_VELAR, P_UVULAR,
    P_PHARYNGEAL, P_EPIGLOTTAL, P_GLOTTAL, P_LABVELAR, P_LABPAL, P_COUNT
};

/* pause kinds */
enum { PZ_WORD = 0, PZ_SHORT, PZ_MINOR, PZ_MAJOR, PZ_STATEMENT, PZ_QUESTION, PZ_EXCLAIM, PZ_COMMA };

#define MAX_TONE 4

typedef struct Seg {
    int kind;
    uint32_t sym;       /* base code point, for analysis output */
    /* vowel features */
    float height;       /* 0 open .. 1 close */
    float back;         /* 0 front .. 1 back */
    float round;        /* 0 .. 1 */
    float rhotic;       /* 0..1 */
    /* consonant features */
    int place, manner, voiced;
    int sibilant;
    int fric_place;     /* affricate release place */
    int fric_sibilant;
    int fric_lateral;
    int fric_voiced;
    /* modifiers */
    float nasal;        /* nasalization amount 0..1 */
    int aspirated;      /* 1 aspirated, 2 breathy-aspirated */
    int preaspirated;
    int breathy, creaky, ejective, unreleased;
    int nasal_release, lateral_release;
    int labialized, palatalized, velarized, pharyngealized, glottalized;
    int prenasal;
    int voiceless_mod;  /* explicit ring: devoiced sonorant/vowel */
    float length;       /* duration multiplier */
    int syllabic;       /* -1 non-syllabic, 0 default, 1 forced syllabic */
    int short_release;  /* inserted secondary segment (superscript) */
    float place_shift;  /* advanced(+)/retracted(-) consonant */
    /* prosody */
    int stress;         /* 0 none, 1 secondary, 2 primary */
    int ntone;
    float tone[MAX_TONE]; /* Chao levels 1..5 */
    int tone_step;      /* +1 upstep, -1 downstep */
    int word_start;
    int pause_kind;     /* for K_PAUSE */
    int nucleus;        /* computed: syllable nucleus */
    int syl;            /* syllable index */
    int unknown;
    /* timing (ms), computed in plan */
    float dur;
    float t0;
} Seg;

typedef struct SegList {
    Seg *s;
    int n, cap;
    int unknown;
} SegList;

int  ipr_parse(const char *utf8, SegList *out);
void ipr_seglist_free(SegList *l);
const char *ipr_place_name(int p);
const char *ipr_manner_name(int m);

/* Parameters for one control frame (1 ms). */
#define NPAR 4
typedef struct Frame {
    float f0;
    float av;       /* voicing amplitude (linear) */
    float ah;       /* aspiration amplitude */
    float af;       /* frication amplitude (parallel) */
    float F[5], B[5];
    float fnp, bnp, fnz, bnz;
    float pf[NPAR], pb[NPAR], pa[NPAR];
    float bypass;   /* flat noise component of frication */
    float oq;       /* open quotient */
    float tilt;     /* 0..1 */
    float creak;    /* 0..1 */
    float breath;   /* breathy noise during voicing */
    float imp;      /* impulse amplitude at this frame (clicks) */
} Frame;

typedef struct Plan {
    Frame *fr;
    int n;
} Plan;

int  ipr_plan(SegList *segs, const ipr_voice *v, const ipr_options *o, Plan *out);
void ipr_plan_free(Plan *p);

/* Render a plan; audio is delivered to cb in float chunks. */
typedef int (*ipr_float_cb)(const float *x, size_t n, void *user);
int ipr_render(const Plan *p, const ipr_voice *v, int sample_rate, float gain, ipr_float_cb cb, void *user);

void ipr_set_unknown(int n);

#endif
