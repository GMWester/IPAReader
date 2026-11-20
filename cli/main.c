/* ipareader command line tool */
#define _POSIX_C_SOURCE 200809L
#include "ipareader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define popen _popen
#define pclose _pclose
#endif

static void usage(void) {
    fprintf(stderr,
        "ipareader %s - speak IPA (International Phonetic Alphabet) text\n\n"
        "usage: ipareader [options] [IPA text]\n"
        "  text is read from the arguments, from -f FILE, or from stdin\n\n"
        "  -w FILE        write a WAV file\n"
        "  --stdout       write WAV to stdout\n"
        "  --raw          write raw 16-bit little-endian mono PCM to stdout\n"
        "  -v VOICE       voice: built-in name (male, female) or path to a .voice file\n"
        "  -s SPEED       speed multiplier (default 1.0)\n"
        "  -p SEMITONES   pitch shift in semitones\n"
        "  -P HZ          set base pitch in Hz\n"
        "  -a VOLUME      volume multiplier (default 1.0)\n"
        "  -r RATE        sample rate (default 22050)\n"
        "  -g MS          extra gap between words in ms\n"
        "  -f FILE        read IPA text from FILE ('-' = stdin)\n"
        "  -x             print the phonetic analysis instead of speaking\n"
        "  --monotone     disable intonation (stress and tones still apply)\n"
        "  --voices       list built-in voices\n"
        "  --dump-voice   print the selected voice as a voice file (start of a new voice)\n"
        "  --version      print version\n\n"
        "With no output option the audio is played through aplay/paplay/afplay.\n"
        "example: ipareader -v female \"ˈhɛloʊ ˈwɝld\"\n", ipr_version());
}

static char *read_all(FILE *f) {
    size_t cap = 4096, n = 0;
    char *b = malloc(cap);
    if (!b) return NULL;
    for (;;) {
        if (n + 4096 >= cap) { cap *= 2; char *nb = realloc(b, cap); if (!nb) { free(b); return NULL; } b = nb; }
        size_t r = fread(b + n, 1, 4096, f);
        n += r;
        if (r == 0) break;
    }
    b[n] = 0;
    return b;
}

static int play(const uint8_t *wav, size_t len) {
    static const char *players[] = {
#ifdef __APPLE__
        NULL,
#endif
        "aplay -q - 2>/dev/null", "paplay 2>/dev/null", "pw-play - 2>/dev/null",
        "ffplay -nodisp -autoexit -loglevel quiet - 2>/dev/null", NULL};
#ifdef __APPLE__
    {
        char path[] = "/tmp/ipareaderXXXXXX";
        int fd = mkstemp(path);
        if (fd >= 0) {
            FILE *f = fdopen(fd, "wb");
            fwrite(wav, 1, len, f); fclose(f);
            char cmd[256]; snprintf(cmd, sizeof cmd, "afplay '%s'", path);
            int rc = system(cmd);
            remove(path);
            if (rc == 0) return 0;
        }
    }
#endif
    for (int i = 0; players[i] || i == 0; i++) {
        if (!players[i]) continue;
        FILE *p = popen(players[i], "w");
        if (!p) continue;
        size_t w = fwrite(wav, 1, len, p);
        int rc = pclose(p);
        if (w == len && rc == 0) return 0;
    }
    fprintf(stderr, "ipareader: no audio player found (aplay/paplay/pw-play/ffplay); use -w FILE or --stdout\n");
    return 1;
}

int main(int argc, char **argv) {
    const char *wav_path = NULL, *voice_name = "male", *in_file = NULL;
    int to_stdout = 0, raw = 0, analyze = 0, dump_voice = 0;
    float pitch_hz = 0;
    ipr_options o;
    ipr_options_default(&o);
    char *text = NULL;
    size_t tlen = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (fprintf(stderr, "ipareader: %s needs a value\n", a), exit(2), (char *)0))
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
        else if (!strcmp(a, "--version")) { printf("%s\n", ipr_version()); return 0; }
        else if (!strcmp(a, "-w")) wav_path = NEXT();
        else if (!strcmp(a, "--stdout")) to_stdout = 1;
        else if (!strcmp(a, "--raw")) raw = 1;
        else if (!strcmp(a, "-v")) voice_name = NEXT();
        else if (!strcmp(a, "-s")) o.speed = (float)atof(NEXT());
        else if (!strcmp(a, "-p")) o.pitch_shift = (float)atof(NEXT());
        else if (!strcmp(a, "-P")) pitch_hz = (float)atof(NEXT());
        else if (!strcmp(a, "-a")) o.volume = (float)atof(NEXT());
        else if (!strcmp(a, "-r")) o.sample_rate = atoi(NEXT());
        else if (!strcmp(a, "-g")) o.word_gap_ms = (float)atof(NEXT());
        else if (!strcmp(a, "-f")) in_file = NEXT();
        else if (!strcmp(a, "-x")) analyze = 1;
        else if (!strcmp(a, "--monotone")) o.flags |= IPR_FLAG_NO_INTONATION;
        else if (!strcmp(a, "--dump-voice")) dump_voice = 1;
        else if (!strcmp(a, "--voices")) {
            for (int k = 0; k < ipr_voice_builtin_count(); k++) printf("%s\n", ipr_voice_builtin_name(k));
            return 0;
        } else if (a[0] == '-' && a[1] && strcmp(a, "--") != 0 && !(a[1] & 0x80)) {
            fprintf(stderr, "ipareader: unknown option %s\n", a); usage(); return 2;
        } else {
            if (!strcmp(a, "--")) { if (++i >= argc) break; a = argv[i]; }
            size_t l = strlen(a);
            char *nt = realloc(text, tlen + l + 2);
            if (!nt) return 1;
            text = nt;
            if (tlen) text[tlen++] = ' ';
            memcpy(text + tlen, a, l + 1);
            tlen += l;
        }
    }

    ipr_voice v;
    if (ipr_voice_builtin(voice_name, &v) != IPR_OK) {
        int rc = ipr_voice_load(voice_name, &v);
        if (rc) { fprintf(stderr, "ipareader: cannot load voice '%s': %s\n", voice_name, ipr_strerror(rc)); return 1; }
    }
    if (pitch_hz > 0) v.pitch = pitch_hz;
    if (dump_voice) {
        char buf[2048]; ipr_voice_format(&v, buf, sizeof buf); fputs(buf, stdout); return 0;
    }

    if (!text) {
        FILE *f = stdin;
        if (in_file && strcmp(in_file, "-")) {
            f = fopen(in_file, "rb");
            if (!f) { perror(in_file); return 1; }
        }
        text = read_all(f);
        if (f != stdin) fclose(f);
        if (!text) return 1;
    }

    if (analyze) {
        int n = ipr_analyze(text, NULL, 0);
        if (n < 0) { fprintf(stderr, "ipareader: %s\n", ipr_strerror(n)); return 1; }
        char *b = malloc((size_t)n + 1);
        ipr_analyze(text, b, (size_t)n + 1);
        fputs(b, stdout);
        free(b); free(text);
        return 0;
    }

    int16_t *pcm; size_t n;
    int rc = ipr_synth(text, &v, &o, &pcm, &n);
    if (rc) { fprintf(stderr, "ipareader: %s\n", ipr_strerror(rc)); free(text); return 1; }
    if (ipr_unknown_count()) fprintf(stderr, "ipareader: warning: %d unrecognised character(s) skipped\n", ipr_unknown_count());

    uint8_t *wav; size_t wl;
    rc = ipr_wav_encode(pcm, n, o.sample_rate, &wav, &wl);
    if (rc) { fprintf(stderr, "ipareader: %s\n", ipr_strerror(rc)); return 1; }
    int status = 0;
#ifdef _WIN32
    if (to_stdout || raw) _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (wav_path) {
        FILE *f = fopen(wav_path, "wb");
        if (!f || fwrite(wav, 1, wl, f) != wl) { perror(wav_path); status = 1; }
        if (f) fclose(f);
    }
    if (raw) fwrite(wav + 44, 1, wl - 44, stdout);
    else if (to_stdout) fwrite(wav, 1, wl, stdout);
    if (!wav_path && !raw && !to_stdout) status = play(wav, wl);
    free(wav); free(pcm); free(text);
    return status;
}
