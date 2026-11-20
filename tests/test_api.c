/* Basic API tests for libipareader. */
#include "ipareader.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static double rms(const int16_t *x, size_t n) {
    double s = 0; for (size_t i = 0; i < n; i++) s += (double)x[i] * x[i];
    return n ? sqrt(s / n) : 0;
}
static size_t streamed;
static int cb(const int16_t *s, size_t n, void *u) { (void)s; (void)u; streamed += n; return 0; }
static int cb_abort(const int16_t *s, size_t n, void *u) { (void)s; (void)n; (void)u; return 1; }

int main(void) {
    ipr_voice m, f, c;
    CHECK(ipr_voice_builtin("male", &m) == IPR_OK);
    CHECK(ipr_voice_builtin("FEMALE", &f) == IPR_OK);
    CHECK(ipr_voice_builtin("nope", &c) == IPR_ERR_VOICE);
    CHECK(ipr_voice_builtin_count() == 2);
    CHECK(f.pitch > m.pitch && f.formant_scale > m.formant_scale);

    /* custom voice from text */
    CHECK(ipr_voice_parse("base = female\npitch = 250 # high\nname=kid\n", &c) == IPR_OK);
    CHECK(fabsf(c.pitch - 250) < 1e-3 && strcmp(c.name, "kid") == 0 && c.formant_scale == f.formant_scale);
    CHECK(ipr_voice_parse("pitch = 9999\n", &c) == IPR_ERR_VOICE);
    CHECK(ipr_voice_parse("bogus = 1\n", &c) == IPR_ERR_VOICE);
    /* format -> parse round trip */
    char buf[2048];
    ipr_voice_format(&f, buf, sizeof buf);
    ipr_voice r;
    CHECK(ipr_voice_parse(buf, &r) == IPR_OK && fabsf(r.pitch - f.pitch) < 1e-3 && fabsf(r.formant_scale - f.formant_scale) < 1e-4);

    int16_t *pcm; size_t n;
    ipr_options o; ipr_options_default(&o);
    CHECK(ipr_synth("ˈhɛloʊ ˈwɝld", &m, &o, &pcm, &n) == IPR_OK);
    CHECK(n > 22050 / 2 && n < 22050 * 3);
    CHECK(rms(pcm, n) > 300);
    size_t n_normal = n;
    ipr_free(pcm);

    o.speed = 2.0f;
    CHECK(ipr_synth("ˈhɛloʊ ˈwɝld", &f, &o, &pcm, &n) == IPR_OK);
    CHECK(n < n_normal);
    ipr_free(pcm);
    ipr_options_default(&o);

    /* sample rates */
    o.sample_rate = 8000;
    CHECK(ipr_synth("a", NULL, &o, &pcm, &n) == IPR_OK); ipr_free(pcm);
    o.sample_rate = 48000;
    CHECK(ipr_synth("a", NULL, &o, &pcm, &n) == IPR_OK); ipr_free(pcm);
    o.sample_rate = 100;
    CHECK(ipr_synth("a", NULL, &o, &pcm, &n) == IPR_ERR_ARG);
    ipr_options_default(&o);

    /* empty input */
    CHECK(ipr_synth("", NULL, NULL, &pcm, &n) == IPR_OK); ipr_free(pcm);
    CHECK(ipr_synth(NULL, NULL, NULL, &pcm, &n) == IPR_ERR_ARG);

    /* unknown characters are counted */
    CHECK(ipr_synth("a€a", NULL, NULL, &pcm, &n) == IPR_OK); ipr_free(pcm);
    CHECK(ipr_unknown_count() == 1);

    /* streaming */
    streamed = 0;
    CHECK(ipr_synth_stream("ˈbɑnʒuʁ", &m, NULL, cb, NULL) == IPR_OK);
    CHECK(streamed > 1000);
    CHECK(ipr_synth_stream("ˈbɑnʒuʁ", &m, NULL, cb_abort, NULL) == IPR_ERR_ABORTED);

    /* WAV */
    uint8_t *w; size_t wl;
    int16_t s[4] = {0, 1, -1, 32767};
    CHECK(ipr_wav_encode(s, 4, 16000, &w, &wl) == IPR_OK && wl == 52 && memcmp(w, "RIFF", 4) == 0);
    ipr_free(w);

    /* analysis */
    int len = ipr_analyze("t͡ʃʰa˥˩", buf, sizeof buf);
    CHECK(len > 0 && strstr(buf, "affricate") && strstr(buf, "aspirated") && strstr(buf, "tone=51"));
    ipr_analyze("ᵐbɓǃkʼ", buf, sizeof buf);
    CHECK(strstr(buf, "prenasalized") && strstr(buf, "implosive") && strstr(buf, "click") && strstr(buf, "ejective"));

    printf(fails ? "%d test(s) failed\n" : "all C API tests passed\n", fails);
    return fails ? 1 : 0;
}
