/* IPA text -> feature segments. */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Symbol tables                                                       */
/* ------------------------------------------------------------------ */

typedef struct { uint32_t cp; float h, b, r, rh; } VowelDef;
typedef struct { uint32_t cp; int place, manner, voiced, sib; } ConsDef;

#define CL 1.0f   /* close */
#define NC 0.83f  /* near-close */
#define CM 0.67f  /* close-mid */
#define MD 0.5f   /* mid */
#define OM 0.33f  /* open-mid */
#define NO 0.17f  /* near-open */
#define OP 0.0f   /* open */

static const VowelDef VOWELS[] = {
    {'i', CL, 0, 0, 0}, {'y', CL, 0, 1, 0}, {0x0268, CL, .5f, 0, 0}, {0x0289, CL, .5f, 1, 0},
    {0x026F, CL, 1, 0, 0}, {'u', CL, 1, 1, 0},
    {0x026A, NC, .2f, 0, 0}, {0x0269, NC, .2f, 0, 0}, {0x028F, NC, .2f, 1, 0},
    {0x1D7B, NC, .5f, 0, 0}, {0x1D7F, NC, .5f, 1, 0}, {0x028A, NC, .8f, 1, 0}, {0x0277, NC, .8f, 1, 0},
    {0x1D1C, NC, .8f, 1, 0},
    {'e', CM, 0, 0, 0}, {0x00F8, CM, 0, 1, 0}, {0x0258, CM, .5f, 0, 0}, {0x0275, CM, .5f, 1, 0},
    {0x0264, CM, 1, 0, 0}, {'o', CM, 1, 1, 0},
    {0x0259, MD, .5f, 0, 0}, {0x01DD, MD, .5f, 0, 0}, {0x025A, MD, .5f, 0, 1},
    {0x025B, OM, 0, 0, 0}, {0x03B5, OM, 0, 0, 0}, {0x0153, OM, 0, 1, 0}, {0x025C, OM, .5f, 0, 0},
    {0x025D, OM, .5f, 0, 1}, {0x025E, OM, .5f, 1, 0}, {0x028C, OM, 1, 0, 0}, {0x0254, OM, 1, 1, 0},
    {0x00E6, NO, 0, 0, 0}, {0x0250, NO, .5f, 0, 0},
    {'a', OP, 0, 0, 0}, {0x0276, OP, 0, 1, 0}, {0x1D00, OP, .5f, 0, 0}, {0x0251, OP, 1, 0, 0},
    {0x03B1, OP, 1, 0, 0}, {0x0252, OP, 1, 1, 0},
    /* Sinological apical vowels */
    {0x027F, CL, .55f, 0, 0}, {0x0285, CL, .55f, 0, .6f}, {0x02AE, CL, .55f, 1, 0}, {0x02AF, CL, .55f, 1, .6f},
};

static const ConsDef CONS[] = {
    /* plosives */
    {'p', P_BILABIAL, M_PLOSIVE, 0, 0}, {'b', P_BILABIAL, M_PLOSIVE, 1, 0},
    {'t', P_ALVEOLAR, M_PLOSIVE, 0, 0}, {'d', P_ALVEOLAR, M_PLOSIVE, 1, 0},
    {0x0288, P_RETROFLEX, M_PLOSIVE, 0, 0}, {0x0256, P_RETROFLEX, M_PLOSIVE, 1, 0},
    {'c', P_PALATAL, M_PLOSIVE, 0, 0}, {0x025F, P_PALATAL, M_PLOSIVE, 1, 0},
    {'k', P_VELAR, M_PLOSIVE, 0, 0}, {'g', P_VELAR, M_PLOSIVE, 1, 0}, {0x0261, P_VELAR, M_PLOSIVE, 1, 0},
    {'q', P_UVULAR, M_PLOSIVE, 0, 0}, {0x0262, P_UVULAR, M_PLOSIVE, 1, 0},
    {0x02A1, P_EPIGLOTTAL, M_PLOSIVE, 0, 0}, {0x0294, P_GLOTTAL, M_PLOSIVE, 0, 0},
    /* nasals */
    {'m', P_BILABIAL, M_NASAL, 1, 0}, {0x0271, P_LABIODENTAL, M_NASAL, 1, 0},
    {'n', P_ALVEOLAR, M_NASAL, 1, 0}, {0x0273, P_RETROFLEX, M_NASAL, 1, 0},
    {0x0272, P_PALATAL, M_NASAL, 1, 0}, {0x014B, P_VELAR, M_NASAL, 1, 0}, {0x0274, P_UVULAR, M_NASAL, 1, 0},
    /* trills, taps */
    {0x0299, P_BILABIAL, M_TRILL, 1, 0}, {'r', P_ALVEOLAR, M_TRILL, 1, 0}, {0x0280, P_UVULAR, M_TRILL, 1, 0},
    {0x2C71, P_LABIODENTAL, M_TAP, 1, 0}, {0x027E, P_ALVEOLAR, M_TAP, 1, 0}, {0x027D, P_RETROFLEX, M_TAP, 1, 0},
    {0x027A, P_ALVEOLAR, M_LATTAP, 1, 0},
    /* fricatives */
    {0x0278, P_BILABIAL, M_FRIC, 0, 0}, {0x03C6, P_BILABIAL, M_FRIC, 0, 0}, {0x03B2, P_BILABIAL, M_FRIC, 1, 0},
    {'f', P_LABIODENTAL, M_FRIC, 0, 0}, {'v', P_LABIODENTAL, M_FRIC, 1, 0},
    {0x03B8, P_DENTAL, M_FRIC, 0, 0}, {0x00F0, P_DENTAL, M_FRIC, 1, 0},
    {'s', P_ALVEOLAR, M_FRIC, 0, 1}, {'z', P_ALVEOLAR, M_FRIC, 1, 1},
    {0x0283, P_POSTALV, M_FRIC, 0, 1}, {0x0292, P_POSTALV, M_FRIC, 1, 1},
    {0x0282, P_RETROFLEX, M_FRIC, 0, 1}, {0x0290, P_RETROFLEX, M_FRIC, 1, 1},
    {0x0255, P_ALVPAL, M_FRIC, 0, 1}, {0x0291, P_ALVPAL, M_FRIC, 1, 1},
    {0x00E7, P_PALATAL, M_FRIC, 0, 0}, {0x029D, P_PALATAL, M_FRIC, 1, 0},
    {'x', P_VELAR, M_FRIC, 0, 0}, {0x0263, P_VELAR, M_FRIC, 1, 0}, {0x0267, P_POSTALV, M_FRIC, 0, 0},
    {0x03C7, P_UVULAR, M_FRIC, 0, 0}, {0x0281, P_UVULAR, M_FRIC, 1, 0},
    {0x0127, P_PHARYNGEAL, M_FRIC, 0, 0}, {0x0295, P_PHARYNGEAL, M_FRIC, 1, 0},
    {0x029C, P_EPIGLOTTAL, M_FRIC, 0, 0}, {0x02A2, P_EPIGLOTTAL, M_FRIC, 1, 0},
    {'h', P_GLOTTAL, M_FRIC, 0, 0}, {0x0266, P_GLOTTAL, M_FRIC, 1, 0},
    {0x026C, P_ALVEOLAR, M_LATFRIC, 0, 0}, {0x026E, P_ALVEOLAR, M_LATFRIC, 1, 0},
    {0xA78E, P_RETROFLEX, M_LATFRIC, 0, 0},
    {0x028D, P_LABVELAR, M_FRIC, 0, 0},
    /* approximants */
    {0x028B, P_LABIODENTAL, M_APPROX, 1, 0}, {0x0279, P_ALVEOLAR, M_APPROX, 1, 0},
    {0x027B, P_RETROFLEX, M_APPROX, 1, 0}, {'j', P_PALATAL, M_APPROX, 1, 0},
    {0x0270, P_VELAR, M_APPROX, 1, 0}, {'w', P_LABVELAR, M_APPROX, 1, 0}, {0x0265, P_LABPAL, M_APPROX, 1, 0},
    {'l', P_ALVEOLAR, M_LATAPPROX, 1, 0}, {0x026D, P_RETROFLEX, M_LATAPPROX, 1, 0},
    {0x028E, P_PALATAL, M_LATAPPROX, 1, 0}, {0x029F, P_VELAR, M_LATAPPROX, 1, 0},
    {0x026B, P_ALVEOLAR, M_LATAPPROX, 1, 0}, /* dark l, velarized below */
    /* implosives */
    {0x0253, P_BILABIAL, M_IMPLOSIVE, 1, 0}, {0x0257, P_ALVEOLAR, M_IMPLOSIVE, 1, 0},
    {0x1D91, P_RETROFLEX, M_IMPLOSIVE, 1, 0}, {0x0284, P_PALATAL, M_IMPLOSIVE, 1, 0},
    {0x0260, P_VELAR, M_IMPLOSIVE, 1, 0}, {0x029B, P_UVULAR, M_IMPLOSIVE, 1, 0},
    /* clicks: sib field = 1 marks lateral */
    {0x0298, P_BILABIAL, M_CLICK, 0, 0}, {0x01C0, P_DENTAL, M_CLICK, 0, 0},
    {0x01C3, P_POSTALV, M_CLICK, 0, 0}, {0x01C2, P_PALATAL, M_CLICK, 0, 0},
    {0x01C1, P_ALVEOLAR, M_CLICK, 0, 1},
    {'!', P_POSTALV, M_CLICK, 0, 0}, /* only used when '!' is inside a word */
};

/* Precomposed characters and ligatures -> sequences of code points. */
typedef struct { uint32_t cp; uint32_t seq[4]; } Decomp;
static const Decomp DECOMP[] = {
    {0x00E0, {'a', 0x0300}}, {0x00E1, {'a', 0x0301}}, {0x00E2, {'a', 0x0302}}, {0x00E3, {'a', 0x0303}},
    {0x00E4, {'a', 0x0308}}, {0x00E5, {'a', 0x030A}}, {0x00E8, {'e', 0x0300}}, {0x00E9, {'e', 0x0301}},
    {0x00EA, {'e', 0x0302}}, {0x00EB, {'e', 0x0308}}, {0x00EC, {'i', 0x0300}}, {0x00ED, {'i', 0x0301}},
    {0x00EE, {'i', 0x0302}}, {0x00EF, {'i', 0x0308}}, {0x00F1, {0x0272}}, {0x00F2, {'o', 0x0300}},
    {0x00F3, {'o', 0x0301}}, {0x00F4, {'o', 0x0302}}, {0x00F5, {'o', 0x0303}}, {0x00F6, {'o', 0x0308}},
    {0x00F9, {'u', 0x0300}}, {0x00FA, {'u', 0x0301}}, {0x00FB, {'u', 0x0302}}, {0x00FC, {'u', 0x0308}},
    {0x00FD, {'y', 0x0301}}, {0x00FF, {'y', 0x0308}},
    {0x0101, {'a', 0x0304}}, {0x0103, {'a', 0x0306}}, {0x0105, {'a', 0x0303}}, {0x0107, {'t', 0x0361, 0x0255}},
    {0x010D, {'t', 0x0361, 0x0283}}, {0x0113, {'e', 0x0304}}, {0x0115, {'e', 0x0306}}, {0x0119, {'e', 0x0303}},
    {0x011B, {'e', 0x030C}}, {0x0129, {'i', 0x0303}}, {0x012B, {'i', 0x0304}}, {0x012D, {'i', 0x0306}},
    {0x012F, {'i', 0x0303}}, {0x0144, {'n', 0x0301}}, {0x0148, {'n', 0x030C}}, {0x014D, {'o', 0x0304}},
    {0x014F, {'o', 0x0306}}, {0x0151, {'o', 0x030B}}, {0x015B, {0x0255}}, {0x0161, {0x0283}},
    {0x0169, {'u', 0x0303}}, {0x016B, {'u', 0x0304}}, {0x016D, {'u', 0x0306}}, {0x016F, {'u', 0x030A}},
    {0x0171, {'u', 0x030B}}, {0x0173, {'u', 0x0303}}, {0x017A, {0x0291}}, {0x017E, {0x0292}},
    {0x01CE, {'a', 0x030C}}, {0x01D0, {'i', 0x030C}}, {0x01D2, {'o', 0x030C}}, {0x01D4, {'u', 0x030C}},
    {0x01D6, {'y', 0x0304}}, {0x01D8, {'y', 0x0301}}, {0x01DA, {'y', 0x030C}}, {0x01DC, {'y', 0x0300}},
    {0x01EB, {'o', 0x0303}}, {0x01F0, {'d', 0x0361, 0x0292}}, {0x01F9, {'n', 0x0300}},
    {0x0201, {'a', 0x030F}}, {0x0205, {'e', 0x030F}}, {0x0209, {'i', 0x030F}}, {0x020D, {'o', 0x030F}},
    {0x0215, {'u', 0x030F}}, {0x0221, {'d', 0x0361, 0x0291}}, {0x0235, {0x0272}}, {0x0236, {'t', 0x0361, 0x0255}},
    {0x1E3F, {'m', 0x0301}}, {0x1EBD, {'e', 0x0303}}, {0x1EF9, {'y', 0x0303}}, {0x1E7D, {'v', 0x0303}},
    {0x1E6D, {0x0288}}, {0x1E0D, {0x0256}}, {0x1E47, {0x0273}}, {0x1E63, {0x0282}}, {0x1E37, {0x026D}},
    {0x1E5B, {0x027D}}, {0x1EA1, {'a'}},
    /* ligatures */
    {0x02A6, {'t', 0x0361, 's'}}, {0x02A3, {'d', 0x0361, 'z'}}, {0x02A7, {'t', 0x0361, 0x0283}},
    {0x02A4, {'d', 0x0361, 0x0292}}, {0x02A8, {'t', 0x0361, 0x0255}}, {0x02A5, {'d', 0x0361, 0x0291}},
    {0x019B, {'t', 0x0361, 0x026C}}, {0x03BB, {'d', 0x0361, 0x026E}}, {0x02AA, {'l', 0x0361, 's'}},
    {0x02AB, {'l', 0x0361, 'z'}}, {0xAB67, {'t', 0x0361, 0x0282}}, {0xAB66, {'d', 0x0361, 0x0290}},
    /* look-alikes */
    {0x03C5, {0x028A}}, {0x03B9, {0x026A}}, {0x0261, {0x0261}}, {0x2019, {0x02BC}},
    {0x0251, {0x0251}}, {0x03C9, {0x0277}}, {0x0D8E, {0}},
    {0x00DF, {'s'}}, {0x00FE, {0x03B8}},
};

/* Superscript (modifier) letters -> base letter they stand for. */
typedef struct { uint32_t sup, base; } SupDef;
static const SupDef SUPS[] = {
    {0x1D43, 'a'}, {0x1D44, 0x0250}, {0x1D45, 0x0251}, {0x1D46, 0x00E6}, {0x1D47, 'b'}, {0x1D48, 'd'},
    {0x1D49, 'e'}, {0x1D4A, 0x0259}, {0x1D4B, 0x025B}, {0x1D4C, 0x025C}, {0x1D4D, 0x0261}, {0x1D4F, 'k'},
    {0x1D50, 'm'}, {0x1D51, 0x014B}, {0x1D52, 'o'}, {0x1D53, 0x0254}, {0x1D56, 'p'}, {0x1D57, 't'},
    {0x1D58, 'u'}, {0x1D5A, 0x026F}, {0x1D5B, 'v'}, {0x1D5D, 0x03B2}, {0x1D5E, 0x0263}, {0x1D5F, 0x00F0},
    {0x1D60, 0x0278}, {0x1D61, 0x03C7}, {0x1D9C, 'c'}, {0x1D9D, 0x0255}, {0x1D9E, 0x00F0}, {0x1D9F, 0x025C},
    {0x1DA0, 'f'}, {0x1DA1, 0x025F}, {0x1DA2, 0x0261}, {0x1DA3, 0x0265}, {0x1DA4, 0x0268}, {0x1DA6, 0x026A},
    {0x1DA8, 0x029D}, {0x1DA9, 0x026D}, {0x1DAA, 0x028E}, {0x1DAB, 0x029F}, {0x1DAC, 0x0271}, {0x1DAD, 0x0270},
    {0x1DAE, 0x0272}, {0x1DAF, 0x0273}, {0x1DB0, 0x0274}, {0x1DB1, 0x0275}, {0x1DB2, 0x0278}, {0x1DB3, 0x0282},
    {0x1DB4, 0x0283}, {0x1DB6, 0x0289}, {0x1DB7, 0x028A}, {0x1DB9, 0x028B}, {0x1DBA, 0x028C}, {0x1DBB, 'z'},
    {0x1DBC, 0x0290}, {0x1DBD, 0x0291}, {0x1DBE, 0x0292}, {0x1DBF, 0x03B8}, {0x02B3, 'r'}, {0x02B4, 0x0279},
    {0x02B5, 0x027B}, {0x02B6, 0x0281}, {0x02B8, 'j'}, {0x02E2, 's'}, {0x02E3, 'x'}, {0x2071, 'i'},
    {0x1D5C, 0x0295}, {0x1D2C, 'a'}, {0x10784, 0x00E6},
};

static const VowelDef *find_vowel(uint32_t cp) {
    for (size_t i = 0; i < sizeof VOWELS / sizeof VOWELS[0]; i++)
        if (VOWELS[i].cp == cp) return &VOWELS[i];
    return NULL;
}
static const ConsDef *find_cons(uint32_t cp) {
    for (size_t i = 0; i < sizeof CONS / sizeof CONS[0]; i++)
        if (CONS[i].cp == cp) return &CONS[i];
    return NULL;
}
static uint32_t find_sup(uint32_t cp) {
    for (size_t i = 0; i < sizeof SUPS / sizeof SUPS[0]; i++)
        if (SUPS[i].sup == cp) return SUPS[i].base;
    return 0;
}

const char *ipr_place_name(int p) {
    static const char *n[] = {"bilabial", "labiodental", "linguolabial", "dental", "alveolar",
        "postalveolar", "retroflex", "alveolopalatal", "palatal", "velar", "uvular", "pharyngeal",
        "epiglottal", "glottal", "labial-velar", "labial-palatal"};
    return (p >= 0 && p < P_COUNT) ? n[p] : "?";
}
const char *ipr_manner_name(int m) {
    static const char *n[] = {"plosive", "nasal", "trill", "tap", "fricative", "lateral-fricative",
        "approximant", "lateral-approximant", "lateral-tap", "implosive", "click", "affricate"};
    return (m >= 0 && m <= M_AFFRICATE) ? n[m] : "?";
}

/* ------------------------------------------------------------------ */
/* UTF-8 decoding with decomposition                                   */
/* ------------------------------------------------------------------ */

typedef struct { uint32_t *c; int n, cap; } CpBuf;

static int cp_push(CpBuf *b, uint32_t c) {
    if (b->n == b->cap) {
        int nc = b->cap ? b->cap * 2 : 64;
        uint32_t *p = realloc(b->c, (size_t)nc * sizeof *p);
        if (!p) return -1;
        b->c = p; b->cap = nc;
    }
    b->c[b->n++] = c;
    return 0;
}

static int decode(const char *s, CpBuf *out) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t c; int len;
        if (*p < 0x80) { c = *p; len = 1; }
        else if ((*p & 0xE0) == 0xC0) { c = *p & 0x1F; len = 2; }
        else if ((*p & 0xF0) == 0xE0) { c = *p & 0x0F; len = 3; }
        else if ((*p & 0xF8) == 0xF0) { c = *p & 0x07; len = 4; }
        else { p++; continue; }
        int ok = 1;
        for (int i = 1; i < len; i++) {
            if ((p[i] & 0xC0) != 0x80) { ok = 0; break; }
            c = (c << 6) | (p[i] & 0x3F);
        }
        if (!ok) { p++; continue; }
        p += len;
        int done = 0;
        for (size_t i = 0; i < sizeof DECOMP / sizeof DECOMP[0]; i++) {
            if (DECOMP[i].cp == c && DECOMP[i].seq[0] != c) {
                for (int k = 0; k < 4 && DECOMP[i].seq[k]; k++)
                    if (cp_push(out, DECOMP[i].seq[k])) return -1;
                done = 1;
                break;
            }
        }
        if (!done && cp_push(out, c)) return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Segment construction                                                */
/* ------------------------------------------------------------------ */

static Seg *push_seg(SegList *l) {
    if (l->n == l->cap) {
        int nc = l->cap ? l->cap * 2 : 64;
        Seg *p = realloc(l->s, (size_t)nc * sizeof *p);
        if (!p) return NULL;
        l->s = p; l->cap = nc;
    }
    Seg *s = &l->s[l->n++];
    memset(s, 0, sizeof *s);
    s->length = 1.0f;
    return s;
}

static int make_seg(uint32_t cp, Seg *s) {
    const VowelDef *v = find_vowel(cp);
    if (v) {
        s->kind = K_VOWEL; s->sym = cp;
        s->height = v->h; s->back = v->b; s->round = v->r; s->rhotic = v->rh;
        s->voiced = 1;
        return 1;
    }
    const ConsDef *c = find_cons(cp);
    if (c) {
        s->kind = K_CONS; s->sym = cp;
        s->place = c->place; s->manner = c->manner; s->voiced = c->voiced;
        s->sibilant = c->sib;
        if (c->manner == M_CLICK) { s->fric_lateral = c->sib; s->sibilant = 0; }
        if (cp == 0x026B) s->velarized = 1;
        if (cp == 0x0267) { s->sibilant = 1; }
        return 1;
    }
    return 0;
}

static int is_combining(uint32_t c) {
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x1DC0 && c <= 0x1DFF) || (c >= 0x1AB0 && c <= 0x1AFF);
}

static int tone_letter(uint32_t c) {
    if (c >= 0x02E5 && c <= 0x02E9) return 5 - (int)(c - 0x02E5);
    if (c >= 0xA708 && c <= 0xA70C) return 5 - (int)(c - 0xA708);
    if (c >= 0xA70D && c <= 0xA711) return 5 - (int)(c - 0xA70D);
    if (c >= 0xA712 && c <= 0xA716) return 5 - (int)(c - 0xA712);
    return 0;
}

static int chao_digit(uint32_t c) {
    switch (c) {
    case 0x00B9: return 1; case 0x00B2: return 2; case 0x00B3: return 3;
    case 0x2074: return 4; case 0x2075: return 5;
    }
    return 0;
}

static int is_vowelish(const Seg *s) {
    return s->kind == K_VOWEL || (s->kind == K_CONS && s->syllabic == 1);
}

static void add_tone(Seg *s, int level) {
    if (s->ntone < MAX_TONE) s->tone[s->ntone++] = (float)level;
}

/* Apply a combining diacritic to segment s. */
static void apply_diacritic(Seg *s, uint32_t c) {
    switch (c) {
    case 0x0300: add_tone(s, 2); break;           /* low */
    case 0x0301: add_tone(s, 4); break;           /* high */
    case 0x0302: add_tone(s, 5); add_tone(s, 1); break; /* falling */
    case 0x0303: case 0x0328: s->nasal = 1.0f; break;
    case 0x0304: add_tone(s, 3); break;           /* mid */
    case 0x0306: s->length *= 0.55f; break;       /* extra short */
    case 0x0308:                                  /* centralized */
        if (s->kind == K_VOWEL) s->back = 0.5f + (s->back - 0.5f) * 0.25f;
        break;
    case 0x033D:                                  /* mid-centralized */
        if (s->kind == K_VOWEL) {
            s->back = 0.5f + (s->back - 0.5f) * 0.5f;
            s->height = 0.5f + (s->height - 0.5f) * 0.5f;
        }
        break;
    case 0x030A: case 0x0325: s->voiced = 0; s->voiceless_mod = 1; break;
    case 0x032C: s->voiced = 1; s->voiceless_mod = 0; break;
    case 0x030B: add_tone(s, 5); break;
    case 0x030C: add_tone(s, 1); add_tone(s, 5); break; /* rising */
    case 0x030D: case 0x0329: s->syllabic = 1; break;
    case 0x030F: add_tone(s, 1); break;
    case 0x0311: case 0x032F: s->syllabic = -1; break;
    case 0x0318: if (s->kind == K_VOWEL) s->height += 0.07f; break;
    case 0x0319: if (s->kind == K_VOWEL) s->height -= 0.07f; break;
    case 0x031A: s->unreleased = 1; break;
    case 0x031C: s->round -= 0.35f; if (s->round < 0) s->round = 0; break;
    case 0x0339: s->round += 0.35f; if (s->round > 1.3f) s->round = 1.3f; break;
    case 0x031D:
        if (s->kind == K_VOWEL) s->height += 0.15f;
        else if (s->manner == M_APPROX || s->manner == M_LATAPPROX) s->manner = (s->manner == M_APPROX) ? M_FRIC : M_LATFRIC;
        break;
    case 0x031E:
        if (s->kind == K_VOWEL) s->height -= 0.15f;
        else if (s->manner == M_FRIC) s->manner = M_APPROX;
        else if (s->manner == M_LATFRIC) s->manner = M_LATAPPROX;
        break;
    case 0x031F: if (s->kind == K_VOWEL) s->back -= 0.2f; else s->place_shift += 1; break;
    case 0x0320: if (s->kind == K_VOWEL) s->back += 0.2f; else s->place_shift -= 1; break;
    case 0x0324: s->breathy = 1; break;
    case 0x0330: s->creaky = 1; break;
    case 0x032A: if (s->kind == K_CONS && s->place == P_ALVEOLAR) s->place = P_DENTAL; break;
    case 0x033C: if (s->kind == K_CONS) s->place = P_LINGUOLABIAL; break;
    case 0x0334: s->velarized = 1; break;
    case 0x0322: s->rhotic = 1.0f; if (s->kind == K_CONS && s->place == P_ALVEOLAR) s->place = P_RETROFLEX; break;
    case 0x0323: if (s->kind == K_CONS && (s->place == P_ALVEOLAR || s->place == P_DENTAL)) s->place = P_RETROFLEX; break;
    case 0x1DC4: add_tone(s, 3); add_tone(s, 5); break;
    case 0x1DC5: add_tone(s, 1); add_tone(s, 3); break;
    case 0x1DC6: add_tone(s, 3); add_tone(s, 1); break;
    case 0x1DC7: add_tone(s, 5); add_tone(s, 3); break;
    case 0x1DC8: add_tone(s, 2); add_tone(s, 4); add_tone(s, 2); break;
    case 0x1DC9: add_tone(s, 4); add_tone(s, 2); add_tone(s, 4); break;
    case 0x033A: case 0x033B: break; /* apical, laminal: accepted, no audible effect modelled */
    default: break;
    }
    if (s->height > 1.15f) s->height = 1.15f;
    if (s->height < -0.1f) s->height = -0.1f;
    if (s->back < -0.15f) s->back = -0.15f;
    if (s->back > 1.15f) s->back = 1.15f;
}

/* Merge b into a for a tie-bar sequence a͡b. */
static void tie_merge(Seg *a, const Seg *b) {
    if (a->kind == K_CONS && b->kind == K_CONS) {
        int bfric = (b->manner == M_FRIC || b->manner == M_LATFRIC);
        if (a->manner == M_PLOSIVE && bfric) {
            a->manner = M_AFFRICATE;
            a->fric_place = b->place;
            a->fric_sibilant = b->sibilant;
            a->fric_lateral = (b->manner == M_LATFRIC);
            a->fric_voiced = b->voiced;
            a->voiced = a->voiced || b->voiced;
            if (b->place == P_POSTALV || b->place == P_ALVPAL || b->place == P_RETROFLEX || b->place == P_PALATAL)
                a->place = b->place;
            a->aspirated |= b->aspirated;
            a->labialized |= b->labialized; a->palatalized |= b->palatalized;
            return;
        }
        if ((a->manner == M_PLOSIVE && b->manner == M_PLOSIVE) || (a->manner == M_NASAL && b->manner == M_NASAL)) {
            int lab = (a->place == P_BILABIAL || b->place == P_BILABIAL);
            int vel = (a->place == P_VELAR || b->place == P_VELAR);
            if (lab && vel) a->place = P_LABVELAR;
            else if (b->place != P_BILABIAL) a->place = b->place;
            a->voiced = a->voiced || b->voiced;
            return;
        }
        if (a->manner == M_NASAL && (b->manner == M_PLOSIVE || b->manner == M_IMPLOSIVE)) {
            /* m͡b: prenasalized stop */
            Seg nb = *b;
            nb.prenasal = 1;
            nb.stress = a->stress; nb.word_start = a->word_start;
            *a = nb;
            return;
        }
    }
    if (a->kind == K_VOWEL && b->kind == K_VOWEL) {
        /* tied vowels: keep as diphthong, handled by caller marking b non-syllabic */
        return;
    }
}

/* Default diphthong detection: in "ai", "au", "ei", "ou"... the high second
 * vowel is a glide. Two vowels of similar height stay separate syllables. */
static void auto_diphthongs(SegList *l) {
    for (int i = 1; i < l->n; i++) {
        Seg *a = &l->s[i - 1], *b = &l->s[i];
        if (a->kind != K_VOWEL || b->kind != K_VOWEL) continue;
        if (b->word_start || b->stress || a->syllabic == -1 || b->syllabic != 0) continue;
        if (a->length > 1.2f) continue;
        if (b->height >= 0.8f && a->height < b->height - 0.12f && b->ntone == 0)
            b->syllabic = -1;
    }
}

static int is_space(uint32_t c) {
    return c == ' ' || c == '\t' || c == 0x00A0 || c == 0x2009 || c == 0x200B || c == 0x3000;
}

int ipr_parse(const char *utf8, SegList *out) {
    CpBuf cb = {0};
    memset(out, 0, sizeof *out);
    if (decode(utf8, &cb)) { free(cb.c); return IPR_ERR_MEMORY; }

    int pending_stress = 0, pending_tie = 0, word_start = 1, after_space = 1;
    int pending_prenasal = 0, pending_preasp = 0, pending_syl_break = 0;
    uint32_t pending_pre_sup = 0;
    int word_first = 0; /* index of first seg in current word */
    int last_base = -1; /* index of last base segment in current word */
    int pending_step = 0;

#define CUR (last_base >= 0 ? &out->s[last_base] : NULL)

    for (int i = 0; i < cb.n; i++) {
        uint32_t c = cb.c[i];
        uint32_t next = (i + 1 < cb.n) ? cb.c[i + 1] : 0;

        if (c == 0) continue;

        /* whitespace & punctuation */
        if (is_space(c) || c == '\n' || c == '\r' || c == '|' || c == 0x2016 || c == ',' || c == ';' ||
            c == '?' || c == '!' || c == '_' || c == 0x3002 || c == 0xFF0C || c == 0x00BF || c == 0x00A1 ||
            c == 0x2026 || c == 0x2014 || c == 0x2013 || (c == '.' && (next == 0 || is_space(next) || next == '\n' || next == '"')) ||
            (c == ':' && after_space)) {
            int kind = -1;
            if (c == '!' && !after_space && next && !is_space(next) && next != '!' && next != '?' && next != '.' && next != ',') {
                /* '!' inside a word: alveolar click (ASCII stand-in for ǃ) */
                goto base;
            }
            if (is_space(c)) kind = PZ_WORD;
            else if (c == '_') kind = PZ_SHORT;
            else if (c == '|') kind = (next == '|') ? PZ_MAJOR : PZ_MINOR;
            else if (c == 0x2016) kind = PZ_MAJOR;
            else if (c == ',' || c == 0xFF0C || c == 0x2014 || c == 0x2013) kind = PZ_COMMA;
            else if (c == ';' || c == ':') kind = PZ_MINOR;
            else if (c == '?') kind = PZ_QUESTION;
            else if (c == '!') kind = PZ_EXCLAIM;
            else if (c == '.' || c == 0x3002 || c == 0x2026) kind = PZ_STATEMENT;
            else if (c == '\n' || c == '\r') kind = PZ_STATEMENT;
            if (c == '|' && next == '|') i++;
            if (kind >= 0) {
                /* merge with previous pause: keep the strongest */
                if (out->n && out->s[out->n - 1].kind == K_PAUSE) {
                    Seg *p = &out->s[out->n - 1];
                    if (kind > p->pause_kind || (kind == PZ_QUESTION || kind == PZ_EXCLAIM)) p->pause_kind = kind;
                } else if (out->n) {
                    Seg *p = push_seg(out);
                    if (!p) goto oom;
                    p->kind = K_PAUSE; p->pause_kind = kind; p->sym = c;
                }
            }
            word_start = 1; after_space = 1; last_base = -1; pending_tie = 0;
            word_first = out->n;
            continue;
        }

        /* brackets and delimiters are ignored */
        if (c == '[' || c == ']' || c == '/' || c == '(' || c == ')' || c == '{' || c == '}' || c == '"' ||
            c == 0x27E8 || c == 0x27E9 || c == '<' || c == '>' || c == '-' || c == 0x2010 || c == 0x2011 ||
            c == 0x201C || c == 0x201D || c == 0x00AB || c == 0x00BB || c == '*' || c == '=' || c == '~' ||
            c == 0x203F || c == 0x0305)
            continue;

        if (c == '.') { pending_syl_break = 1; continue; }

        /* stress */
        if (c == 0x02C8 || c == 0x02CC || c == 0x02BA ||
            (c == '\'' && (after_space || (last_base >= 0 && out->s[last_base].kind == K_VOWEL))) || c == 0x00B4) {
            pending_stress = (c == 0x02CC) ? 1 : 2;
            if (c == 0x02BA) pending_stress = 2;
            continue;
        }
        if (c == ',' ) continue;

        /* tie bars */
        if (c == 0x0361 || c == 0x035C || c == 0x203F) { if (last_base >= 0) pending_tie = 1; continue; }

        /* tone letters */
        int tl = tone_letter(c);
        int cd = chao_digit(c);
        if (!cd && c >= '1' && c <= '5' && !after_space && last_base >= 0) cd = (int)(c - '0');
        if (tl || cd) {
            int lvl = tl ? tl : cd;
            /* attach to the last syllabic segment in this word */
            int k;
            for (k = out->n - 1; k >= word_first && k >= 0; k--)
                if (out->s[k].kind != K_PAUSE && is_vowelish(&out->s[k]) && out->s[k].syllabic != -1) break;
            if (k < word_first || k < 0) {
                for (k = out->n - 1; k >= word_first && k >= 0; k--)
                    if (out->s[k].kind != K_PAUSE && out->s[k].voiced) break;
            }
            if (k >= word_first && k >= 0) {
                Seg *s = &out->s[k];
                /* a contour is written letter by letter; a new run after another
                 * syllable starts a new tone */
                add_tone(s, lvl);
            }
            continue;
        }
        if (c == 0xA71B) { pending_step = 1; continue; }   /* upstep */
        if (c == 0xA71C) { pending_step = -1; continue; }  /* downstep */
        if (c == 0x2197 || c == 0x2198) continue;           /* global rise/fall: ignored */

        /* length */
        if (c == 0x02D0 || (c == ':' && last_base >= 0) || c == 0x02F8) {
            if (CUR) CUR->length *= (CUR->kind == K_VOWEL) ? 1.85f : 1.7f;
            continue;
        }
        if (c == 0x02D1) { if (CUR) CUR->length *= 1.35f; continue; }

        /* combining diacritics */
        if (is_combining(c)) {
            if (CUR) apply_diacritic(CUR, c);
            continue;
        }

        /* spacing modifier letters */
        if (c == 0x02B0 || c == 0x02B1) {
            int breathy = (c == 0x02B1);
            Seg *p = CUR;
            int next_is_cons = 0;
            { Seg t; memset(&t, 0, sizeof t); if (make_seg(next, &t) && t.kind == K_CONS) next_is_cons = 1; }
            if (p && p->kind == K_CONS && p->manner != M_NASAL && p->manner != M_APPROX) {
                p->aspirated = breathy ? 2 : 1;
            } else if (next_is_cons) {
                pending_preasp = 1;
            } else if (p) {
                p->aspirated = breathy ? 2 : 1;
            }
            continue;
        }
        if (c == 0x02B7 || c == 0x1DA3) { if (CUR) { CUR->labialized = 1; if (c == 0x1DA3) CUR->palatalized = 1; } continue; }
        if (c == 0x02B2) { if (CUR) CUR->palatalized = 1; continue; }
        if (c == 0x02E0) { if (CUR) CUR->velarized = 1; continue; }
        if (c == 0x02E4 || c == 0x02C1) { if (CUR) CUR->pharyngealized = 1; continue; }
        if (c == 0x02C0) { if (CUR) CUR->glottalized = 1; continue; }
        if (c == 0x02BC || (c == '\'' && last_base >= 0)) { if (CUR) CUR->ejective = 1; continue; }
        if (c == 0x02DE) { if (CUR) CUR->rhotic = 1.0f; continue; }
        if (c == 0x207F || c == 0x02E1) {
            Seg *p = CUR;
            int lat = (c == 0x02E1);
            if (p && p->kind == K_CONS && (p->manner == M_PLOSIVE || p->manner == M_AFFRICATE)) {
                if (lat) p->lateral_release = 1; else p->nasal_release = 1;
                continue;
            }
            if (!lat) { pending_prenasal = 1; continue; }
            c = 'l';
            goto superscript_general;
        }
        {
            uint32_t sb = find_sup(c);
            if (sb) {
                c = sb;
superscript_general:;
                Seg t; memset(&t, 0, sizeof t); t.length = 1;
                if (!make_seg(c, &t)) continue;
                if (t.kind == K_CONS && t.manner == M_NASAL && (last_base < 0 || out->s[last_base].kind == K_VOWEL)) {
                    /* ᵐb, ᵑɡ: prenasalization of the next consonant */
                    pending_prenasal = 1;
                    continue;
                }
                if (last_base < 0) { pending_pre_sup = c; continue; }
                Seg *s = push_seg(out);
                if (!s) goto oom;
                *s = t;
                s->short_release = 1;
                s->length = 0.45f;
                s->syllabic = -1;
                /* keep last_base on the host segment so later diacritics still attach to it */
                continue;
            }
        }

base:
        {
            Seg t; memset(&t, 0, sizeof t); t.length = 1;
            if (!make_seg(c, &t)) {
                /* unknown character: count and skip */
                if (c > ' ' && !(c >= '0' && c <= '9')) out->unknown++;
                continue;
            }
            if (pending_tie && last_base >= 0) {
                Seg *a = &out->s[last_base];
                if (a->kind == K_VOWEL && t.kind == K_VOWEL) {
                    Seg *s = push_seg(out);
                    if (!s) goto oom;
                    *s = t; s->syllabic = -1;
                    last_base = out->n - 1;
                } else {
                    tie_merge(a, &t);
                    /* plosive+plosive or affricate: keep a single segment */
                }
                pending_tie = 0;
                after_space = 0;
                continue;
            }
            if (pending_pre_sup) {
                Seg *s = push_seg(out);
                if (!s) goto oom;
                memset(s, 0, sizeof *s);
                make_seg(pending_pre_sup, s);
                s->length = 0.45f; s->short_release = 1; s->syllabic = -1;
                s->word_start = word_start; word_start = 0;
                s->stress = pending_stress; pending_stress = 0;
                pending_pre_sup = 0;
            }
            Seg *s = push_seg(out);
            if (!s) goto oom;
            *s = t;
            s->word_start = word_start;
            s->stress = pending_stress;
            s->tone_step = pending_step;
            s->prenasal = pending_prenasal;
            s->preaspirated = pending_preasp;
            if (pending_syl_break && !s->stress) s->stress = -1; /* syllable boundary marker */
            pending_stress = 0; pending_prenasal = 0; pending_preasp = 0; pending_step = 0; pending_syl_break = 0;
            word_start = 0; after_space = 0;
            last_base = out->n - 1;
        }
    }
#undef CUR
    free(cb.c);
    /* drop trailing word pause */
    while (out->n && out->s[out->n - 1].kind == K_PAUSE && out->s[out->n - 1].pause_kind == PZ_WORD) out->n--;
    auto_diphthongs(out);
    return IPR_OK;
oom:
    free(cb.c);
    ipr_seglist_free(out);
    return IPR_ERR_MEMORY;
}

void ipr_seglist_free(SegList *l) {
    free(l->s);
    l->s = NULL; l->n = l->cap = 0;
}
