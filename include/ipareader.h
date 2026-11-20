/*
 * IPAReader - speech synthesis directly from International Phonetic Alphabet text.
 *
 * Every IPA letter is described by articulatory features (place, manner,
 * voicing, vowel height/backness/rounding). Diacritics and suprasegmentals
 * modify those features, and a formant synthesizer turns the features into
 * audio. No language rules, no dictionaries, no neural networks: what you write
 * in IPA is what gets spoken.
 *
 * Copyright (C) 2026 IPAReader contributors. Licensed under the GNU AGPL v3.
 */
#ifndef IPAREADER_H
#define IPAREADER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(IPR_BUILD_DLL)
#define IPR_API __declspec(dllexport)
#elif defined(_WIN32) && defined(IPR_USE_DLL)
#define IPR_API __declspec(dllimport)
#elif defined(__GNUC__)
#define IPR_API __attribute__((visibility("default")))
#else
#define IPR_API
#endif

#define IPR_VERSION "1.0.0"

/* Error codes */
#define IPR_OK            0
#define IPR_ERR_ARG      -1
#define IPR_ERR_MEMORY   -2
#define IPR_ERR_IO       -3
#define IPR_ERR_VOICE    -4
#define IPR_ERR_ABORTED  -5

/*
 * A voice. The built-in voices are "male" and "female"; new voices can be
 * made by filling this struct directly, by starting from a built-in voice and
 * changing fields, or by loading a voice file (see ipr_voice_load).
 */
typedef struct ipr_voice {
    char  name[32];
    float pitch;          /* base fundamental frequency in Hz (male ~110, female ~205) */
    float pitch_range;    /* intonation range multiplier (1.0 = normal) */
    float formant_scale;  /* vocal tract scaling of all formants (1.0 male, ~1.17 female) */
    float formant_shift[5]; /* per-formant extra multipliers for F1..F5 (1.0 = none) */
    float bandwidth_scale;/* formant bandwidth multiplier (1.0 = normal) */
    float open_quotient;  /* glottal open quotient 0.3..0.9 (higher = softer/breathier) */
    float spectral_tilt;  /* 0..1, higher = darker/softer voice */
    float breathiness;    /* 0..1 aspiration noise mixed into voicing */
    float jitter;         /* 0..0.05 random pitch perturbation */
    float shimmer;        /* 0..0.2 random amplitude perturbation */
    float vibrato_depth;  /* semitones */
    float vibrato_rate;   /* Hz */
    float speed;          /* speaking rate multiplier (1.0 = normal, 2.0 = twice as fast) */
    float volume;         /* output gain multiplier (1.0 = normal) */
    float frication_gain; /* multiplier for fricative/burst noise loudness */
} ipr_voice;

/* Synthesis options that are not part of the voice. */
typedef struct ipr_options {
    int   sample_rate;    /* output sample rate, 8000..96000 (default 22050) */
    float speed;          /* extra speed multiplier on top of the voice (default 1.0) */
    float pitch_shift;    /* semitones added to the voice pitch (default 0) */
    float volume;         /* extra gain multiplier (default 1.0) */
    float word_gap_ms;    /* extra silence between words (default 0) */
    int   flags;          /* IPR_FLAG_* */
} ipr_options;

#define IPR_FLAG_NO_LEADING_SILENCE  1  /* do not add 30 ms silence at start/end */
#define IPR_FLAG_NO_INTONATION       2  /* monotone apart from stress and tone marks */

/* ---- voices ---- */

/* Fill *v with a built-in voice ("male", "female", case-insensitive).
 * Returns IPR_OK or IPR_ERR_VOICE if the name is unknown. */
IPR_API int ipr_voice_builtin(const char *name, ipr_voice *v);

/* Number of built-in voices and their names (index 0..count-1). */
IPR_API int ipr_voice_builtin_count(void);
IPR_API const char *ipr_voice_builtin_name(int index);

/* Parse a voice description: "key = value" lines, '#' comments.
 * A line "base = female" starts from that built-in voice. Unknown keys are
 * errors. See the voices directory for examples. */
IPR_API int ipr_voice_parse(const char *text, ipr_voice *v);
IPR_API int ipr_voice_load(const char *path, ipr_voice *v);

/* Write a voice as voice-file text into buf (returns length needed, like snprintf). */
IPR_API int ipr_voice_format(const ipr_voice *v, char *buf, size_t buflen);

/* ---- synthesis ---- */

IPR_API void ipr_options_default(ipr_options *o);

/* Synthesize IPA text (UTF-8). On success *samples receives a malloc'd buffer
 * of 16-bit mono PCM that the caller frees with ipr_free(). voice/opts may be
 * NULL for defaults (male voice, 22050 Hz). */
IPR_API int ipr_synth(const char *ipa_utf8, const ipr_voice *voice,
                      const ipr_options *opts, int16_t **samples, size_t *nsamples);

/* Same but 32-bit float samples in [-1, 1]. */
IPR_API int ipr_synth_float(const char *ipa_utf8, const ipr_voice *voice,
                            const ipr_options *opts, float **samples, size_t *nsamples);

/* Streaming: audio is delivered in chunks to cb. Return non-zero from cb to
 * abort (ipr_synth_stream then returns IPR_ERR_ABORTED). */
typedef int (*ipr_audio_cb)(const int16_t *samples, size_t n, void *user);
IPR_API int ipr_synth_stream(const char *ipa_utf8, const ipr_voice *voice,
                             const ipr_options *opts, ipr_audio_cb cb, void *user);

/* Synthesize straight to a WAV file. */
IPR_API int ipr_synth_to_wav(const char *ipa_utf8, const ipr_voice *voice,
                             const ipr_options *opts, const char *path);

/* Encode PCM as a WAV file in memory (malloc'd, free with ipr_free). */
IPR_API int ipr_wav_encode(const int16_t *samples, size_t n, int sample_rate,
                           uint8_t **wav, size_t *wav_len);

/* Describe how the text is analysed (one line per segment), for debugging.
 * Returns the number of bytes needed (like snprintf). */
IPR_API int ipr_analyze(const char *ipa_utf8, char *buf, size_t buflen);

/* Number of input characters that were not recognised in the last call
 * made by this thread to ipr_synth or ipr_analyze. */
IPR_API int ipr_unknown_count(void);

IPR_API void ipr_free(void *p);
IPR_API const char *ipr_version(void);
IPR_API const char *ipr_strerror(int err);

#ifdef __cplusplus
}
#endif
#endif
