/* Formant synthesizer: glottal source + cascade vocal tract + parallel frication branch. */
#include "internal.h"
#include "tune.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { double a, b, c, y1, y2; } Reson;      /* cascade (unity DC gain) */
typedef struct { double a, b, c, x1, x2; } Anti;       /* antiresonator */
typedef struct { double g, b, c, x1, x2, y1, y2, u1, u2, v1, v2; } Band; /* parallel: two identical sections, unity peak gain */

static void reson_set(Reson *r, double f, double bw, double fs) {
    if (f <= 0 || f >= fs * 0.47) { r->a = 1; r->b = 0; r->c = 0; return; }
    double rr = exp(-M_PI * bw / fs);
    r->c = -rr * rr;
    r->b = 2 * rr * cos(2 * M_PI * f / fs);
    r->a = 1 - r->b - r->c;
}
static double reson_run(Reson *r, double x) {
    double y = r->a * x + r->b * r->y1 + r->c * r->y2;
    r->y2 = r->y1; r->y1 = y;
    return y;
}
static void anti_set(Anti *r, double f, double bw, double fs) {
    if (f <= 0 || f >= fs * 0.47) { r->a = 1; r->b = 0; r->c = 0; return; }
    double rr = exp(-M_PI * bw / fs);
    double c = -rr * rr, b = 2 * rr * cos(2 * M_PI * f / fs), a = 1 - b - c;
    r->a = 1 / a; r->b = -b / a; r->c = -c / a;
}
static double anti_run(Anti *r, double x) {
    double y = r->a * x + r->b * r->x1 + r->c * r->x2;
    r->x2 = r->x1; r->x1 = x;
    return y;
}
static void band_set(Band *r, double f, double bw, double fs) {
    bw *= 1.55; /* two sections in series narrow the band */
    if (f <= 0 || f >= fs * 0.47) { r->g = 0; r->b = 0; r->c = 0; return; }
    double rr = exp(-M_PI * bw / fs);
    r->c = -rr * rr;
    r->b = 2 * rr * cos(2 * M_PI * f / fs);
    r->g = (1 - rr * rr) * 0.5;
}
static double band_run(Band *r, double x) {
    double y = r->g * (x - r->x2) + r->b * r->y1 + r->c * r->y2;
    r->x2 = r->x1; r->x1 = x;
    r->y2 = r->y1; r->y1 = y;
    double z = r->g * (y - r->u2) + r->b * r->v1 + r->c * r->v2;
    r->u2 = r->u1; r->u1 = y;
    r->v2 = r->v1; r->v1 = z;
    return z;
}

typedef struct { double b0, b1, b2, a1, a2, x1, x2, y1, y2; } Biquad;
/* RBJ cookbook filters */
static void bq_lowshelf(Biquad *q, double f, double gain_db, double fs) {
    double A = pow(10, gain_db / 40), w = 2 * M_PI * f / fs, c = cos(w), sn = sin(w);
    double al = sn / 2 * sqrt(2.0), sa = 2 * sqrt(A) * al;
    double a0 = (A + 1) + (A - 1) * c + sa;
    q->b0 = A * ((A + 1) - (A - 1) * c + sa) / a0; q->b1 = 2 * A * ((A - 1) - (A + 1) * c) / a0;
    q->b2 = A * ((A + 1) - (A - 1) * c - sa) / a0; q->a1 = -2 * ((A - 1) + (A + 1) * c) / a0;
    q->a2 = ((A + 1) + (A - 1) * c - sa) / a0;
}
static void bq_peak(Biquad *q, double f, double Q, double gain_db, double fs) {
    double A = pow(10, gain_db / 40), w = 2 * M_PI * f / fs, c = cos(w), al = sin(w) / (2 * Q);
    double a0 = 1 + al / A;
    q->b0 = (1 + al * A) / a0; q->b1 = -2 * c / a0; q->b2 = (1 - al * A) / a0;
    q->a1 = -2 * c / a0; q->a2 = (1 - al / A) / a0;
}
static double bq_run(Biquad *q, double x) {
    double y = q->b0 * x + q->b1 * q->x1 + q->b2 * q->x2 - q->a1 * q->y1 - q->a2 * q->y2;
    q->x2 = q->x1; q->x1 = x; q->y2 = q->y1; q->y1 = y;
    return y;
}

typedef struct { uint64_t s; } Rng;
static double rng_uniform(Rng *r) { /* xorshift64*, [-1, 1) */
    r->s ^= r->s >> 12; r->s ^= r->s << 25; r->s ^= r->s >> 27;
    uint64_t v = r->s * 2685821657736338717ULL;
    return ((v >> 11) * (1.0 / 9007199254740992.0)) * 2.0 - 1.0;
}
static double rng_gauss(Rng *r) {
    return (rng_uniform(r) + rng_uniform(r) + rng_uniform(r)) * 0.577;
}

#define CHUNK 1024
#ifndef NCASC
#define NCASC 7
#endif
#ifndef RAD_COEF
#define RAD_COEF 0.8
#endif
#ifndef CASC_GAIN
#define CASC_GAIN 0.085
#endif
#ifndef PAR_GAIN
#define PAR_GAIN 0.68
#endif

int ipr_render(const Plan *p, const ipr_voice *v, int fs, float gain, ipr_float_cb cb, void *user) {
    Reson casc[7], np;
    Anti nz;
    Band par[NPAR];
    memset(casc, 0, sizeof casc); memset(&np, 0, sizeof np); memset(&nz, 0, sizeof nz); memset(par, 0, sizeof par);
    Rng rng = {0x9E3779B97F4A7C15ULL};
    double phase = 0, period_f0 = 100, amp_jit = 1, tiltz = 0, hp_x = 0, hp_y = 0, noise_lp = 0;
    double vib_ph = 0, rad_prev = 0;
    float buf[CHUNK];
    int bn = 0;
    if (p->n < 2) return IPR_OK;
    double total_ms = p->n - 1;
    size_t nsamp = (size_t)(total_ms * fs / 1000.0);
    double hp_a = exp(-2 * M_PI * 45.0 / fs);
    int pending_imp_frame = -1;
    ipr_tune_init();
    double rad_coef = T(RAD);
    double ret_ms = T(RET_MS), hfn = T(HFN), hf_prev = 0;
    /* voice spectral balance: tame the low end, lift the 1-3 kHz presence region */
    double eq_gain = T(EQ_GAIN), noise_gain = T(NOISE_GAIN), flutter = T(FLUTTER);
    Biquad eq_lo, eq_mid;
    memset(&eq_lo, 0, sizeof eq_lo); memset(&eq_mid, 0, sizeof eq_mid);
    bq_lowshelf(&eq_lo, T(EQ_LO_F), T(EQ_LO_DB), fs);
    bq_peak(&eq_mid, T(EQ_MID_F), 0.7, T(EQ_MID_DB), fs);
    double pulse_scale = 1.0;

    for (size_t n = 0; n < nsamp; n++) {
        double tms = n * 1000.0 / fs;
        int fi = (int)tms;
        if (fi >= p->n - 1) fi = p->n - 2;
        double u = tms - fi;
        const Frame *A = &p->fr[fi], *B = &p->fr[fi + 1];
#define IP(x) (A->x + (B->x - A->x) * u)
        double f0 = IP(f0), av = IP(av), ah = IP(ah), af = IP(af);
        double oq = IP(oq), tilt = IP(tilt), creak = IP(creak), breath = IP(breath);

        if ((n & 7) == 0) {
            for (int k = 0; k < 5; k++) reson_set(&casc[k], IP(F[k]), IP(B[k]), fs);
            double fsc = IP(F[3]) / 3500.0;
            reson_set(&casc[5], 5500 * fsc, 500, fs);
            reson_set(&casc[6], 6600 * fsc, 700, fs);
            reson_set(&np, IP(fnp), IP(bnp), fs);
            anti_set(&nz, IP(fnz), IP(bnz), fs);
            for (int k = 0; k < NPAR; k++) band_set(&par[k], IP(pf[k]), IP(pb[k]), fs);
        }

        /* ---- glottal source ---- */
        vib_ph += v->vibrato_rate / fs;
        if (vib_ph >= 1) vib_ph -= 1;
        double tsec = (double)n / fs;
        /* flutter: slow, irregular pitch wander (sum of incommensurate sines) */
        double fl = flutter * (sin(2 * M_PI * 12.7 * tsec) + sin(2 * M_PI * 7.1 * tsec) + sin(2 * M_PI * 4.7 * tsec)) / 3.0;
        double f0v = f0 * pow(2.0, (v->vibrato_depth * sin(2 * M_PI * vib_ph) + fl) / 12.0);
        phase += period_f0 / fs;
        if (phase >= 1.0) {
            phase -= 1.0;
            double j = 1 + v->jitter * rng_uniform(&rng);
            double fc = f0v * j;
            if (creak > 0.01) fc *= (1 - 0.45 * creak) * (1 + 0.3 * creak * rng_uniform(&rng));
            if (fc < 25) fc = 25;
            period_f0 = fc;
            amp_jit = 1 + v->shimmer * rng_uniform(&rng);
            if (creak > 0.01) amp_jit *= 1 - 0.4 * creak * fabs(rng_uniform(&rng));
            /* keep loudness roughly independent of f0 (higher voices have more pulses per second) */
            pulse_scale = pow(120.0 / fc, 0.7);
        }
        double oqc = oq - 0.25 * creak;
        if (oqc < 0.2) oqc = 0.2;
        if (oqc > 0.95) oqc = 0.95;
        double g = 0;
        int open = phase < oqc;
        if (open) {
            double x = phase / oqc;
            g = (2 * x - 3 * x * x) * 3.0 / sqrt(oqc);
        } else if (ret_ms > 0) {
            /* return phase: the folds close gradually rather than instantly (less buzzy) */
            double ret = ret_ms * period_f0 / 1000.0;          /* as a fraction of the period */
            g = -3.0 / sqrt(oqc) * exp(-(phase - oqc) / ret);
        }
        /* spectral tilt: one-pole low-pass */
        double ta = 0.85 * (tilt < 0 ? 0 : tilt > 1 ? 1 : tilt);
        tiltz = (1 - ta) * g + ta * tiltz;
        double voice = tiltz * av * amp_jit * pulse_scale * (1 + ta * 2.5);

        double noise = rng_gauss(&rng);
        /* aspiration: slightly low-passed noise */
        noise_lp = 0.6 * noise + 0.4 * noise_lp;
        double asp = noise_lp * (ah * 3.5 + breath * av * (open ? 0.4 : 0.12));

        /* pulse-synchronous high-frequency breath noise, as in natural voices */
        double hf = (noise - hf_prev) * 0.5; hf_prev = noise;
        double src = voice + asp + hf * hfn * av * (open ? 1.0 : 0.3);
        double x = reson_run(&np, src);
        x = anti_run(&nz, x);
        for (int k = 0; k < NCASC; k++) x = reson_run(&casc[k], x);
        /* lip radiation: first difference (+6 dB/octave) */
        double cascade = x - rad_coef * rad_prev;
        rad_prev = x;
        cascade = bq_run(&eq_mid, bq_run(&eq_lo, cascade));

        /* ---- parallel frication branch ---- */
        double fn = rng_gauss(&rng);
        if (av > 0.05) fn *= open ? 1.0 : 0.45; /* voiced fricatives: noise modulated by voicing */
        double imp = 0;
        if (A->imp > 0 && pending_imp_frame != fi) { imp = A->imp * 2.5; pending_imp_frame = fi; }
        double pin = fn * af + imp;
        double par_out = 0;
        if (af > 1e-5 || imp != 0 || fabs(par[0].y1) > 1e-7 || fabs(par[1].y1) > 1e-7) {
            for (int k = 0; k < NPAR; k++) {
                double pa = IP(pa[k]);
                double y = band_run(&par[k], pin);
                par_out += pa * y;
            }
            par_out += IP(bypass) * fn * af;
        }
#undef IP
        double out = (cascade * CASC_GAIN + par_out * PAR_GAIN * noise_gain) * eq_gain;
        /* DC blocker */
        double y = out - hp_x + hp_a * hp_y;
        hp_x = out; hp_y = y;
        double s = y * gain * v->volume;
        /* soft limiter */
        if (s > 0.7) s = 0.7 + 0.3 * tanh((s - 0.7) / 0.3);
        else if (s < -0.7) s = -0.7 + 0.3 * tanh((s + 0.7) / 0.3);
        buf[bn++] = (float)s;
        if (bn == CHUNK) {
            if (cb(buf, (size_t)bn, user)) return IPR_ERR_ABORTED;
            bn = 0;
        }
    }
    if (bn && cb(buf, (size_t)bn, user)) return IPR_ERR_ABORTED;
    return IPR_OK;
}
