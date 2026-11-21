# IPAReader

A speech synthesizer that reads **raw IPA** (International Phonetic Alphabet) and pronounces exactly what is written, for any language.

- **No language rules, no dictionaries, no neural network / LLM.** Each IPA symbol is decoded into articulatory features (place, manner, voicing, vowel height/backness/rounding). Diacritics, length, stress and tone change those features, and a formant synthesizer turns them into sound. Because pronunciation comes from the features, every combination works, including symbol + diacritic combinations no language uses.
- **Deterministic.** The same input always gives the same audio.
- **Embeddable like espeak.** A small C99 library (`libipareader.so` / `.a` / `.dylib` / `.dll`) with no dependencies beyond libm. There is a stable C API, a command-line tool, and a Python binding. It works from any language that can call C.
- **Voices.** Built-in `male` and `female` voices. You can make new voices from a text file, a struct, or a dict.
- Fast: about 100× faster than real time on one core. Output can be any sample rate from 8 to 96 kHz. Synthesis can stream.

## Build

```bash
make            # builds build/ipareader, build/libipareader.so, build/libipareader.a
make test       # C and Python tests
sudo make install
```

You need a C11 compiler. Windows (MinGW/MSVC) and macOS are supported; on macOS the library is `libipareader.dylib`.

## Command line

```bash
ipareader "ˈhɛloʊ ˈwɝld"                        # play (aplay/paplay/pw-play/ffplay/afplay)
ipareader -v female -w hello.wav "həˈloʊ"        # write WAV
ipareader -v voices/child.voice "ni˧˥ xɑʊ˨˩˦"    # custom voice file
echo "bɔ̃ʒuʁ" | ipareader --stdout > out.wav     # pipe: WAV on stdout
ipareader --raw -r 16000 "ʔaloha" | your-program # raw 16-bit PCM
ipareader -x "t͡ʃʰa˥˩ ᵐba kʷʼ"                    # show how the text was analysed
```

| Option | Meaning |
|---|---|
| `-w FILE` | write a WAV file |
| `--stdout` / `--raw` | WAV / raw s16le PCM to stdout |
| `-v VOICE` | `male`, `female`, or a path to a `.voice` file |
| `-s SPEED` | speed multiplier (1.0 is normal) |
| `-p SEMITONES` | pitch shift; `-P HZ` sets the base pitch |
| `-a VOLUME` | volume multiplier |
| `-r RATE` | sample rate (default 22050) |
| `-g MS` | extra silence between words |
| `-f FILE` | read text from a file (`-` means stdin) |
| `-x` | print the phonetic analysis |
| `--monotone` | no sentence intonation (stress and tones still apply) |
| `--voices`, `--dump-voice` | list built-in voices; print the current voice as a voice file |

## C API

```c
#include <ipareader.h>

ipr_voice v;
ipr_voice_builtin("female", &v);         /* or ipr_voice_load("my.voice", &v) */
ipr_options o;
ipr_options_default(&o);                 /* 22050 Hz, speed 1 */

int16_t *pcm; size_t n;
if (ipr_synth("ˈhɛloʊ ˈwɝld", &v, &o, &pcm, &n) == IPR_OK) {
    /* ... use n samples of 16-bit mono PCM ... */
    ipr_free(pcm);
}
ipr_synth_to_wav("bɔ̃ʒuʁ", &v, &o, "out.wav");
```

Link with `-lipareader -lm`. The full API is in [`include/ipareader.h`](include/ipareader.h):

| Function | Purpose |
|---|---|
| `ipr_synth` / `ipr_synth_float` | synthesize into a malloc'd buffer (int16 or float) |
| `ipr_synth_stream` | stream chunks to a callback; the callback can abort |
| `ipr_synth_to_wav`, `ipr_wav_encode` | WAV output, to a file or to memory |
| `ipr_voice_builtin`, `ipr_voice_load`, `ipr_voice_parse`, `ipr_voice_format` | voices |
| `ipr_analyze` | text description of how the input was parsed |
| `ipr_unknown_count` | number of unrecognised characters in the last call |

The library is thread-safe: there is no global state apart from a thread-local unknown-character counter.

## Python

```python
import sys; sys.path.insert(0, "python")   # or copy python/ipareader.py next to your code
import ipareader

tts = ipareader.IPAReader(voice="female", sample_rate=22050, speed=1.0)
pcm = tts.synth("ˈhɛloʊ ˈwɝld")            # bytes, 16-bit mono
tts.save("ˈhɛloʊ ˈwɝld", "hello.wav")
wav_bytes = tts.wav("ʃaˈlom")
tts.stream("ˈbɑnʒuʁ", lambda chunk: player.write(chunk))
custom = ipareader.IPAReader(voice={"base": "male", "pitch": 95, "speed": 0.9})
print(ipareader.analyze("t͡ʃʰa˥˩"))
```

The binding uses `ctypes`, so nothing needs compiling. It finds the library through `$IPAREADER_LIB`, next to the module, in `../build`, or on the system library path.

## Other languages

Any FFI can call the library. All strings are UTF-8 `char*` and audio is `int16_t*`. Examples:

- **Node.js** (koffi): `const lib = koffi.load('libipareader.so'); const f = lib.func('int ipr_synth_to_wav(str, void*, void*, str)'); f('ˈhɛloʊ', null, null, 'out.wav');`
- **C#**: `[DllImport("ipareader")] static extern int ipr_synth_to_wav(byte[] utf8, IntPtr voice, IntPtr opts, string path);` and pass `Encoding.UTF8.GetBytes(text + "\0")`.
- **Rust**: `extern "C" { fn ipr_synth_to_wav(t: *const c_char, v: *const c_void, o: *const c_void, p: *const c_char) -> c_int; }`
- **Any language, no FFI:** run `ipareader --stdout "…"` as a subprocess and read the WAV it writes.

Passing `NULL` for the voice or options uses the defaults (male voice, 22050 Hz).

## Voices

The library has two built-in voices, `male` and `female`. A voice is a set of parameters; a voice file is plain text:

```ini
# voices/child.voice
name = child
base = female          # start from a built-in voice
pitch = 270            # base F0 in Hz
formant_scale = 1.28   # shorter vocal tract
breathiness = 0.12
speed = 1.05
```

| Key | Range | Effect |
|---|---|---|
| `base` | male, female | starting voice |
| `pitch` | 30–600 | base pitch (Hz) |
| `pitch_range` | 0–4 | intonation and tone range (0 gives a monotone) |
| `formant_scale` | 0.6–1.8 | vocal tract size (1.0 male, ~1.13 female, ~1.3 child) |
| `f1_shift` … `f5_shift` | 0.5–2 | multiplier for one formant |
| `bandwidth_scale` | 0.3–4 | formant bandwidths (higher is muffled, lower is ringing) |
| `open_quotient` | 0.3–0.95 | glottal open phase (higher is softer, lower is pressed/buzzy) |
| `spectral_tilt` | 0–1 | darkness of the voice |
| `breathiness` | 0–1 | aspiration noise in voicing |
| `jitter`, `shimmer` | 0–0.1, 0–0.5 | pitch and amplitude irregularity |
| `vibrato_depth`, `vibrato_rate` | semitones, Hz | vibrato |
| `speed` | 0.1–6 | speaking rate |
| `volume`, `frication_gain` | 0–8 | overall loudness; loudness of fricatives and bursts |

`voices/` has `male.voice` and `female.voice`, which are copies of the built-ins to start from, plus the examples `child`, `deep` and `robot`. `ipareader -v X --dump-voice` prints any voice in this format. In code you can fill an `ipr_voice` struct directly or call `ipr_voice_parse()` on a string.

## How it works

1. **Parse.** The UTF-8 input is decoded. Precomposed letters (ã, é, ǎ…) and ligatures (ʧ, ʦ…) are split, and every base symbol is looked up in a feature table. Combining diacritics and modifier letters then edit the segment's features: nasalization, voicing, aspiration, secondary articulation, advanced/retracted, raised/lowered, rounding, length, syllabicity, tone and so on. Tie bars create affricates, doubly articulated stops, and prenasalized stops.
2. **Prosody.** Syllables are built from vowels and syllabic consonants. The input's stress marks, length marks, tone letters/diacritics/digits and punctuation set the duration and the pitch contour: declination, stress accents, statement fall, question rise, and Chao tone levels 1–5.
3. **Acoustics.** Each segment becomes formant targets and source settings. Vowel formants are interpolated continuously over height × backness × rounding, so diacritic-shifted vowels land between the cardinal vowels. Consonants get place-dependent loci, coarticulated with neighbouring vowels; noise spectra per place; bursts, aspiration (VOT), voice bars, nasal murmur with a pole–zero pair, lateral and rhotic formants, trill pulsing, ejective glottal holds, implosive voicing, and click transients.
4. **Synthesis.** A glottal pulse source drives the vocal tract. It has a gradual closing (return) phase, pulse-synchronous breath noise, jitter, shimmer, vibrato, and creak. That source drives a cascade of 7 formant resonators plus a nasal pole/zero pair. Frication goes through a parallel bank of band-pass resonators. Parameters are updated every millisecond and interpolated per sample.

## Supported IPA characters

Unrecognised characters are skipped. They are counted by `ipr_unknown_count()`, and the CLI prints a warning for them. The square or slash brackets around a transcription are ignored, as are `( ) { } " - *`.

<!-- SYMBOLS:BEGIN -->
### Vowels

| Symbol | Code point | Description |
|---|---|---|
| i | U+0069 | close front unrounded |
| y | U+0079 | close front rounded |
| ɨ | U+0268 | close central unrounded |
| ʉ | U+0289 | close central rounded |
| ɯ | U+026F | close back unrounded |
| u | U+0075 | close back rounded |
| ɪ | U+026A | near-close near-front unrounded |
| ɩ | U+0269 | near-close near-front unrounded |
| ʏ | U+028F | near-close near-front rounded |
| ᵻ | U+1D7B | near-close central unrounded |
| ᵿ | U+1D7F | near-close central rounded |
| ʊ | U+028A | near-close near-back rounded |
| ɷ | U+0277 | near-close near-back rounded |
| ᴜ | U+1D1C | near-close near-back rounded |
| e | U+0065 | close-mid front unrounded |
| ø | U+00F8 | close-mid front rounded |
| ɘ | U+0258 | close-mid central unrounded |
| ɵ | U+0275 | close-mid central rounded |
| ɤ | U+0264 | close-mid back unrounded |
| o | U+006F | close-mid back rounded |
| ə | U+0259 | mid central unrounded |
| ǝ | U+01DD | mid central unrounded |
| ɚ | U+025A | mid central unrounded r-coloured |
| ɛ | U+025B | open-mid front unrounded |
| ε | U+03B5 | open-mid front unrounded |
| œ | U+0153 | open-mid front rounded |
| ɜ | U+025C | open-mid central unrounded |
| ɝ | U+025D | open-mid central unrounded r-coloured |
| ɞ | U+025E | open-mid central rounded |
| ʌ | U+028C | open-mid back unrounded |
| ɔ | U+0254 | open-mid back rounded |
| æ | U+00E6 | near-open front unrounded |
| ɐ | U+0250 | near-open central unrounded |
| a | U+0061 | open front unrounded |
| ɶ | U+0276 | open front rounded |
| ᴀ | U+1D00 | open central unrounded |
| ɑ | U+0251 | open back unrounded |
| α | U+03B1 | open back unrounded |
| ɒ | U+0252 | open back rounded |
| ɿ | U+027F | close central (apical) unrounded |
| ʅ | U+0285 | close central (apical) unrounded r-coloured |
| ʮ | U+02AE | close central (apical) rounded |
| ʯ | U+02AF | close central (apical) rounded r-coloured |

### Consonants

| Symbol | Code point | Description |
|---|---|---|
| p | U+0070 | voiceless bilabial plosive |
| b | U+0062 | voiced bilabial plosive |
| t | U+0074 | voiceless alveolar plosive |
| d | U+0064 | voiced alveolar plosive |
| ʈ | U+0288 | voiceless retroflex plosive |
| ɖ | U+0256 | voiced retroflex plosive |
| c | U+0063 | voiceless palatal plosive |
| ɟ | U+025F | voiced palatal plosive |
| k | U+006B | voiceless velar plosive |
| g | U+0067 | voiced velar plosive (ASCII g, same as ɡ) |
| ɡ | U+0261 | voiced velar plosive |
| q | U+0071 | voiceless uvular plosive |
| ɢ | U+0262 | voiced uvular plosive |
| ʡ | U+02A1 | voiceless epiglottal plosive |
| ʔ | U+0294 | voiceless glottal plosive |
| m | U+006D | voiced bilabial nasal |
| ɱ | U+0271 | voiced labiodental nasal |
| n | U+006E | voiced alveolar nasal |
| ɳ | U+0273 | voiced retroflex nasal |
| ɲ | U+0272 | voiced palatal nasal |
| ŋ | U+014B | voiced velar nasal |
| ɴ | U+0274 | voiced uvular nasal |
| ʙ | U+0299 | voiced bilabial trill |
| r | U+0072 | voiced alveolar trill |
| ʀ | U+0280 | voiced uvular trill |
| ⱱ | U+2C71 | voiced labiodental tap/flap |
| ɾ | U+027E | voiced alveolar tap/flap |
| ɽ | U+027D | voiced retroflex tap/flap |
| ɺ | U+027A | voiced alveolar lateral flap |
| ɸ | U+0278 | voiceless bilabial fricative |
| φ | U+03C6 | voiceless bilabial fricative |
| β | U+03B2 | voiced bilabial fricative |
| f | U+0066 | voiceless labiodental fricative |
| v | U+0076 | voiced labiodental fricative |
| θ | U+03B8 | voiceless dental fricative |
| ð | U+00F0 | voiced dental fricative |
| s | U+0073 | voiceless alveolar sibilant fricative |
| z | U+007A | voiced alveolar sibilant fricative |
| ʃ | U+0283 | voiceless postalveolar sibilant fricative |
| ʒ | U+0292 | voiced postalveolar sibilant fricative |
| ʂ | U+0282 | voiceless retroflex sibilant fricative |
| ʐ | U+0290 | voiced retroflex sibilant fricative |
| ɕ | U+0255 | voiceless alveolo-palatal sibilant fricative |
| ʑ | U+0291 | voiced alveolo-palatal sibilant fricative |
| ç | U+00E7 | voiceless palatal fricative |
| ʝ | U+029D | voiced palatal fricative |
| x | U+0078 | voiceless velar fricative |
| ɣ | U+0263 | voiced velar fricative |
| ɧ | U+0267 | voiceless postalveolar fricative |
| χ | U+03C7 | voiceless uvular fricative |
| ʁ | U+0281 | voiced uvular fricative |
| ħ | U+0127 | voiceless pharyngeal fricative |
| ʕ | U+0295 | voiced pharyngeal fricative |
| ʜ | U+029C | voiceless epiglottal fricative |
| ʢ | U+02A2 | voiced epiglottal fricative |
| h | U+0068 | voiceless glottal fricative |
| ɦ | U+0266 | voiced glottal fricative |
| ɬ | U+026C | voiceless alveolar lateral fricative |
| ɮ | U+026E | voiced alveolar lateral fricative |
| ꞎ | U+A78E | voiceless retroflex lateral fricative |
| ʍ | U+028D | voiceless labial-velar fricative |
| ʋ | U+028B | voiced labiodental approximant |
| ɹ | U+0279 | voiced alveolar approximant |
| ɻ | U+027B | voiced retroflex approximant |
| j | U+006A | voiced palatal approximant |
| ɰ | U+0270 | voiced velar approximant |
| w | U+0077 | voiced labial-velar approximant |
| ɥ | U+0265 | voiced labial-palatal approximant |
| l | U+006C | voiced alveolar lateral approximant |
| ɭ | U+026D | voiced retroflex lateral approximant |
| ʎ | U+028E | voiced palatal lateral approximant |
| ʟ | U+029F | voiced velar lateral approximant |
| ɫ | U+026B | voiced alveolar lateral approximant (velarized, dark l) |
| ɓ | U+0253 | voiced bilabial implosive |
| ɗ | U+0257 | voiced alveolar implosive |
| ᶑ | U+1D91 | voiced retroflex implosive |
| ʄ | U+0284 | voiced palatal implosive |
| ɠ | U+0260 | voiced velar implosive |
| ʛ | U+029B | voiced uvular implosive |
| ʘ | U+0298 | bilabial click |
| ǀ | U+01C0 | dental click |
| ǃ | U+01C3 | postalveolar click |
| ǂ | U+01C2 | palatal click |
| ǁ | U+01C1 | alveolar lateral click |

### Ligatures, precomposed letters and accepted look-alikes

These are rewritten to the IPA sequence shown before analysis.

| Input | Code point | Read as |
|---|---|---|
| à | U+00E0 | à |
| á | U+00E1 | á |
| â | U+00E2 | â |
| ã | U+00E3 | ã |
| ä | U+00E4 | ä |
| å | U+00E5 | å |
| è | U+00E8 | è |
| é | U+00E9 | é |
| ê | U+00EA | ê |
| ë | U+00EB | ë |
| ì | U+00EC | ì |
| í | U+00ED | í |
| î | U+00EE | î |
| ï | U+00EF | ï |
| ñ | U+00F1 | ɲ |
| ò | U+00F2 | ò |
| ó | U+00F3 | ó |
| ô | U+00F4 | ô |
| õ | U+00F5 | õ |
| ö | U+00F6 | ö |
| ù | U+00F9 | ù |
| ú | U+00FA | ú |
| û | U+00FB | û |
| ü | U+00FC | ü |
| ý | U+00FD | ý |
| ÿ | U+00FF | ÿ |
| ā | U+0101 | ā |
| ă | U+0103 | ă |
| ą | U+0105 | ã |
| ć | U+0107 | t͡ɕ |
| č | U+010D | t͡ʃ |
| ē | U+0113 | ē |
| ĕ | U+0115 | ĕ |
| ę | U+0119 | ẽ |
| ě | U+011B | ě |
| ĩ | U+0129 | ĩ |
| ī | U+012B | ī |
| ĭ | U+012D | ĭ |
| į | U+012F | ĩ |
| ń | U+0144 | ń |
| ň | U+0148 | ň |
| ō | U+014D | ō |
| ŏ | U+014F | ŏ |
| ő | U+0151 | ő |
| ś | U+015B | ɕ |
| š | U+0161 | ʃ |
| ũ | U+0169 | ũ |
| ū | U+016B | ū |
| ŭ | U+016D | ŭ |
| ů | U+016F | ů |
| ű | U+0171 | ű |
| ų | U+0173 | ũ |
| ź | U+017A | ʑ |
| ž | U+017E | ʒ |
| ǎ | U+01CE | ǎ |
| ǐ | U+01D0 | ǐ |
| ǒ | U+01D2 | ǒ |
| ǔ | U+01D4 | ǔ |
| ǖ | U+01D6 | ȳ |
| ǘ | U+01D8 | ý |
| ǚ | U+01DA | y̌ |
| ǜ | U+01DC | ỳ |
| ǫ | U+01EB | õ |
| ǰ | U+01F0 | d͡ʒ |
| ǹ | U+01F9 | ǹ |
| ȁ | U+0201 | ȁ |
| ȅ | U+0205 | ȅ |
| ȉ | U+0209 | ȉ |
| ȍ | U+020D | ȍ |
| ȕ | U+0215 | ȕ |
| ȡ | U+0221 | d͡ʑ |
| ȵ | U+0235 | ɲ |
| ȶ | U+0236 | t͡ɕ |
| ḿ | U+1E3F | ḿ |
| ẽ | U+1EBD | ẽ |
| ỹ | U+1EF9 | ỹ |
| ṽ | U+1E7D | ṽ |
| ṭ | U+1E6D | ʈ |
| ḍ | U+1E0D | ɖ |
| ṇ | U+1E47 | ɳ |
| ṣ | U+1E63 | ʂ |
| ḷ | U+1E37 | ɭ |
| ṛ | U+1E5B | ɽ |
| ạ | U+1EA1 | a |
| ʦ | U+02A6 | t͡s |
| ʣ | U+02A3 | d͡z |
| ʧ | U+02A7 | t͡ʃ |
| ʤ | U+02A4 | d͡ʒ |
| ʨ | U+02A8 | t͡ɕ |
| ʥ | U+02A5 | d͡ʑ |
| ƛ | U+019B | t͡ɬ |
| λ | U+03BB | d͡ɮ |
| ʪ | U+02AA | l͡s |
| ʫ | U+02AB | l͡z |
| ꭧ | U+AB67 | t͡ʂ |
| ꭦ | U+AB66 | d͡ʐ |
| υ | U+03C5 | ʊ |
| ι | U+03B9 | ɪ |
| ’ | U+2019 | ʼ |
| ω | U+03C9 | ɷ |
| ß | U+00DF | s |
| þ | U+00FE | θ |

### Superscript letters

A superscript letter after a sound is pronounced as a short release of that sound (e.g. tˢ, kˣ, dᶻ, tᵊ). A superscript nasal before a consonant prenasalizes it (ᵐb, ⁿd, ᵑɡ); ⁿ and ˡ after a plosive give nasal / lateral release. ʰ ʱ ʷ ʲ ˠ ˤ are listed under diacritics.

| Symbol | Code point | Short |
|---|---|---|
| ᵃ | U+1D43 | a |
| ᵄ | U+1D44 | ɐ |
| ᵅ | U+1D45 | ɑ |
| ᵆ | U+1D46 | æ |
| ᵇ | U+1D47 | b |
| ᵈ | U+1D48 | d |
| ᵉ | U+1D49 | e |
| ᵊ | U+1D4A | ə |
| ᵋ | U+1D4B | ɛ |
| ᵌ | U+1D4C | ɜ |
| ᵍ | U+1D4D | ɡ |
| ᵏ | U+1D4F | k |
| ᵐ | U+1D50 | m |
| ᵑ | U+1D51 | ŋ |
| ᵒ | U+1D52 | o |
| ᵓ | U+1D53 | ɔ |
| ᵖ | U+1D56 | p |
| ᵗ | U+1D57 | t |
| ᵘ | U+1D58 | u |
| ᵚ | U+1D5A | ɯ |
| ᵛ | U+1D5B | v |
| ᵝ | U+1D5D | β |
| ᵞ | U+1D5E | ɣ |
| ᵟ | U+1D5F | ð |
| ᵠ | U+1D60 | ɸ |
| ᵡ | U+1D61 | χ |
| ᶜ | U+1D9C | c |
| ᶝ | U+1D9D | ɕ |
| ᶞ | U+1D9E | ð |
| ᶟ | U+1D9F | ɜ |
| ᶠ | U+1DA0 | f |
| ᶡ | U+1DA1 | ɟ |
| ᶢ | U+1DA2 | ɡ |
| ᶣ | U+1DA3 | ɥ |
| ᶤ | U+1DA4 | ɨ |
| ᶦ | U+1DA6 | ɪ |
| ᶨ | U+1DA8 | ʝ |
| ᶩ | U+1DA9 | ɭ |
| ᶪ | U+1DAA | ʎ |
| ᶫ | U+1DAB | ʟ |
| ᶬ | U+1DAC | ɱ |
| ᶭ | U+1DAD | ɰ |
| ᶮ | U+1DAE | ɲ |
| ᶯ | U+1DAF | ɳ |
| ᶰ | U+1DB0 | ɴ |
| ᶱ | U+1DB1 | ɵ |
| ᶲ | U+1DB2 | ɸ |
| ᶳ | U+1DB3 | ʂ |
| ᶴ | U+1DB4 | ʃ |
| ᶶ | U+1DB6 | ʉ |
| ᶷ | U+1DB7 | ʊ |
| ᶹ | U+1DB9 | ʋ |
| ᶺ | U+1DBA | ʌ |
| ᶻ | U+1DBB | z |
| ᶼ | U+1DBC | ʐ |
| ᶽ | U+1DBD | ʑ |
| ᶾ | U+1DBE | ʒ |
| ᶿ | U+1DBF | θ |
| ʳ | U+02B3 | r |
| ʴ | U+02B4 | ɹ |
| ʵ | U+02B5 | ɻ |
| ʶ | U+02B6 | ʁ |
| ʸ | U+02B8 | j |
| ˢ | U+02E2 | s |
| ˣ | U+02E3 | x |
| ⁱ | U+2071 | i |
| ᵜ | U+1D5C | ʕ |
| ᴬ | U+1D2C | a |
| 𐞄 | U+10784 | æ |

### Diacritics and modifier letters

| Symbol | Code point | Meaning |
|---|---|---|
| ʰ | U+02B0 | aspirated (after a plosive); preaspiration if written before a consonant |
| ʱ | U+02B1 | breathy-voiced aspiration |
| ʷ | U+02B7 | labialized |
| ʲ | U+02B2 | palatalized |
| ˠ | U+02E0 | velarized |
| ˤ | U+02E4 | pharyngealized |
| ˁ | U+02C1 | pharyngealized (alternative) |
| ˀ | U+02C0 | glottalized |
| ᶣ | U+1DA3 | labial-palatalized |
| ⁿ | U+207F | nasal release (after plosive) / prenasalization (before consonant) |
| ˡ | U+02E1 | lateral release |
| ʼ | U+02BC | ejective (also ' after a consonant, and ’) |
| ˞ | U+02DE | rhoticity (r-colouring) |
| ◌̥ | U+0325 | voiceless |
| ◌̊ | U+030A | voiceless (above) |
| ◌̬ | U+032C | voiced |
| ◌̤ | U+0324 | breathy voiced |
| ◌̰ | U+0330 | creaky voiced |
| ◌̃ | U+0303 | nasalized |
| ◌̨ | U+0328 | nasalized (ogonek, Americanist) |
| ◌̩ | U+0329 | syllabic |
| ◌̍ | U+030D | syllabic (above) |
| ◌̯ | U+032F | non-syllabic |
| ◌̑ | U+0311 | non-syllabic (above) |
| ◌̟ | U+031F | advanced |
| ◌̠ | U+0320 | retracted |
| ◌̈ | U+0308 | centralized |
| ◌̽ | U+033D | mid-centralized |
| ◌̝ | U+031D | raised (approximant → fricative) |
| ◌̞ | U+031E | lowered (fricative → approximant) |
| ◌̹ | U+0339 | more rounded |
| ◌̜ | U+031C | less rounded |
| ◌̘ | U+0318 | advanced tongue root |
| ◌̙ | U+0319 | retracted tongue root |
| ◌̪ | U+032A | dental |
| ◌̼ | U+033C | linguolabial |
| ◌̺ | U+033A | apical (accepted, no audible change) |
| ◌̻ | U+033B | laminal (accepted, no audible change) |
| ◌̚ | U+031A | no audible release |
| ◌̴ | U+0334 | velarized or pharyngealized |
| ◌̢ | U+0322 | retroflex / r-coloured |
| ◌̣ | U+0323 | retroflex (Indological dot below) |
| ◌̆ | U+0306 | extra-short |
| ◌͡ | U+0361 | tie bar: affricates t͡s, double articulation k͡p, prenasalized m͡b, diphthongs a͡i |
| ◌͜ | U+035C | tie bar (below) |

### Suprasegmentals and punctuation

| Symbol | Code point | Meaning |
|---|---|---|
| ˈ | U+02C8 | primary stress (also ASCII ' before a syllable) |
| ˌ | U+02CC | secondary stress |
| ː | U+02D0 | long (also ASCII : after a sound) |
| ˑ | U+02D1 | half-long |
| . | U+002E | syllable break (end of sentence when followed by space) |
| \| | U+007C | minor (foot) group pause |
| ‖ | U+2016 | major (intonation) group pause (also ||) |
| ‿ | U+203F | linking (no break) |
| ꜛ | U+A71B | upstep |
| ꜜ | U+A71C | downstep |
| ↗ | U+2197 | global rise (accepted) |
| ↘ | U+2198 | global fall (accepted) |
| , | U+002C | short pause with continuation rise |
| ? | U+003F | question intonation |
| ! | U+0021 | exclamation (inside a word: alveolar click ǃ) |
| _ | U+005F | short pause |
| ; | U+003B | pause |

### Tones

| Symbol | Code point | Meaning |
|---|---|---|
| ˥ | U+02E5 | tone letter extra high (5) |
| ˦ | U+02E6 | tone letter high (4) |
| ˧ | U+02E7 | tone letter mid (3) |
| ˨ | U+02E8 | tone letter low (2) |
| ˩ | U+02E9 | tone letter extra low (1) |
| ◌̋ | U+030B | extra-high tone |
| ◌́ | U+0301 | high tone |
| ◌̄ | U+0304 | mid tone |
| ◌̀ | U+0300 | low tone |
| ◌̏ | U+030F | extra-low tone |
| ◌̌ | U+030C | rising tone |
| ◌̂ | U+0302 | falling tone |
| ◌᷄ | U+1DC4 | high (mid-high) rising |
| ◌᷅ | U+1DC5 | low rising |
| ◌᷆ | U+1DC6 | low falling |
| ◌᷇ | U+1DC7 | high falling |
| ◌᷈ | U+1DC8 | rising-falling |
| ◌᷉ | U+1DC9 | falling-rising |
| ¹ | U+00B9 | Chao tone digit 1 (also ²³⁴⁵ and ASCII 1–5 after a syllable) |

Contour tones are written as sequences of tone letters (˧˥, ˨˩˦, ˥˩) or digits (ma⁵¹, ma51). Tone letters A708–A716 (dotted / left-stem forms) are read as the same five levels.
<!-- SYMBOLS:END -->

## Measuring intelligibility

`tools/eval/asr_eval.py` synthesizes 102 English and 161 sentences in 13 other languages from IPA. It has them transcribed by Whisper (running locally via sherpa-onnx) and reports word and character error rates. Setup instructions are at the top of the script. Recent results:

| Voice | English WER | 13-language CER |
|---|---|---|
| male | ~9–10% | ~18% |
| female | ~9–11% | ~23% |
| espeak-ng reading the *text* (reference) | 25% | 27% |

Most of the multilingual test IPA comes from espeak-ng's text-to-IPA, so a few transcriptions are themselves imperfect. Whisper is also weak on short Arabic, Hindi and Mandarin sentences. Short sentences in those languages swing a lot between runs.

Internal acoustic constants are listed in `src/tune.h`. You can override them for experiments with the environment variable `IPR_TUNE="NAME=value,NAME=value"`.

## Limitations

- IPAReader is a formant synthesizer. It is clear and precise, but it sounds synthetic. It is not a neural voice.
- It does not convert ordinary spelling to IPA. The input must already be IPA.
- Phonation types, clicks and pharyngeals are approximations of the acoustics, not physical simulations.

## License

GNU AGPL v3 (see [LICENSE](LICENSE)).
