"""Python binding for libipareader (ctypes, no compilation needed).

    import ipareader
    tts = ipareader.IPAReader(voice="female")
    pcm = tts.synth("ˈhɛloʊ ˈwɝld")          # bytes, 16-bit mono PCM
    tts.save("ˈhɛloʊ ˈwɝld", "hello.wav")
    wav = tts.wav("ˈhɛloʊ ˈwɝld")            # bytes of a complete WAV file

The shared library is searched in $IPAREADER_LIB, next to this file, in
../build and in the system library path.
"""
import ctypes
import ctypes.util
import os
import sys

__all__ = ["IPAReader", "Voice", "builtin_voices", "analyze", "version", "IPAReaderError"]


class IPAReaderError(RuntimeError):
    pass


class Voice(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char * 32),
        ("pitch", ctypes.c_float),
        ("pitch_range", ctypes.c_float),
        ("formant_scale", ctypes.c_float),
        ("formant_shift", ctypes.c_float * 5),
        ("bandwidth_scale", ctypes.c_float),
        ("open_quotient", ctypes.c_float),
        ("spectral_tilt", ctypes.c_float),
        ("breathiness", ctypes.c_float),
        ("jitter", ctypes.c_float),
        ("shimmer", ctypes.c_float),
        ("vibrato_depth", ctypes.c_float),
        ("vibrato_rate", ctypes.c_float),
        ("speed", ctypes.c_float),
        ("volume", ctypes.c_float),
        ("frication_gain", ctypes.c_float),
    ]

    def to_text(self):
        buf = ctypes.create_string_buffer(4096)
        _lib().ipr_voice_format(ctypes.byref(self), buf, len(buf))
        return buf.value.decode()


class _Options(ctypes.Structure):
    _fields_ = [
        ("sample_rate", ctypes.c_int),
        ("speed", ctypes.c_float),
        ("pitch_shift", ctypes.c_float),
        ("volume", ctypes.c_float),
        ("word_gap_ms", ctypes.c_float),
        ("flags", ctypes.c_int),
    ]


FLAG_NO_LEADING_SILENCE = 1
FLAG_NO_INTONATION = 2

_LIB = None
_CB = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t, ctypes.c_void_p)


def _lib():
    global _LIB
    if _LIB is not None:
        return _LIB
    names = {"darwin": "libipareader.dylib", "win32": "ipareader.dll"}.get(sys.platform, "libipareader.so")
    here = os.path.dirname(os.path.abspath(__file__))
    cands = [os.environ.get("IPAREADER_LIB"), os.path.join(here, names),
             os.path.join(here, "..", "build", names), ctypes.util.find_library("ipareader")]
    err = None
    for c in cands:
        if not c:
            continue
        try:
            lib = ctypes.CDLL(c)
            break
        except OSError as e:
            err = e
    else:
        raise IPAReaderError("libipareader not found (build it with `make`, or set IPAREADER_LIB): %s" % err)
    P, I, S, F = ctypes.POINTER, ctypes.c_int, ctypes.c_size_t, ctypes.c_char_p
    lib.ipr_voice_builtin.argtypes = [F, P(Voice)]
    lib.ipr_voice_builtin_count.restype = I
    lib.ipr_voice_builtin_name.restype = F
    lib.ipr_voice_builtin_name.argtypes = [I]
    lib.ipr_voice_parse.argtypes = [F, P(Voice)]
    lib.ipr_voice_load.argtypes = [F, P(Voice)]
    lib.ipr_voice_format.argtypes = [P(Voice), ctypes.c_char_p, S]
    lib.ipr_options_default.argtypes = [P(_Options)]
    lib.ipr_options_default.restype = None
    lib.ipr_synth.argtypes = [F, P(Voice), P(_Options), P(P(ctypes.c_int16)), P(S)]
    lib.ipr_synth_stream.argtypes = [F, P(Voice), P(_Options), _CB, ctypes.c_void_p]
    lib.ipr_wav_encode.argtypes = [P(ctypes.c_int16), S, I, P(P(ctypes.c_uint8)), P(S)]
    lib.ipr_analyze.argtypes = [F, ctypes.c_char_p, S]
    lib.ipr_unknown_count.restype = I
    lib.ipr_free.argtypes = [ctypes.c_void_p]
    lib.ipr_free.restype = None
    lib.ipr_version.restype = F
    lib.ipr_strerror.restype = F
    lib.ipr_strerror.argtypes = [I]
    _LIB = lib
    return lib


def _check(rc):
    if rc != 0:
        raise IPAReaderError(_lib().ipr_strerror(rc).decode())


def version():
    return _lib().ipr_version().decode()


def builtin_voices():
    lib = _lib()
    return [lib.ipr_voice_builtin_name(i).decode() for i in range(lib.ipr_voice_builtin_count())]


def load_voice(voice):
    """Return a Voice from a built-in name, a .voice file path, voice-file text, or a dict of fields."""
    lib = _lib()
    v = Voice()
    if isinstance(voice, Voice):
        return voice
    if isinstance(voice, dict):
        d = dict(voice)
        base = d.pop("base", "male")
        _check(lib.ipr_voice_builtin(base.encode(), ctypes.byref(v)))
        for k, val in d.items():
            if k == "name":
                v.name = val.encode()[:31]
            elif k.startswith("f") and k.endswith("_shift"):
                v.formant_shift[int(k[1]) - 1] = val
            elif hasattr(v, k):
                setattr(v, k, val)
            else:
                raise IPAReaderError("unknown voice field: %s" % k)
        return v
    if lib.ipr_voice_builtin(voice.encode(), ctypes.byref(v)) == 0:
        return v
    if "=" in voice:
        _check(lib.ipr_voice_parse(voice.encode(), ctypes.byref(v)))
    else:
        _check(lib.ipr_voice_load(voice.encode(), ctypes.byref(v)))
    return v


def analyze(ipa):
    lib = _lib()
    n = lib.ipr_analyze(ipa.encode(), None, 0)
    if n < 0:
        _check(n)
    buf = ctypes.create_string_buffer(n + 1)
    lib.ipr_analyze(ipa.encode(), buf, n + 1)
    return buf.value.decode()


class IPAReader:
    def __init__(self, voice="male", sample_rate=22050, speed=1.0, pitch_shift=0.0, volume=1.0,
                 word_gap_ms=0.0, intonation=True):
        self.voice = load_voice(voice)
        self.opts = _Options()
        _lib().ipr_options_default(ctypes.byref(self.opts))
        self.opts.sample_rate = sample_rate
        self.opts.speed = speed
        self.opts.pitch_shift = pitch_shift
        self.opts.volume = volume
        self.opts.word_gap_ms = word_gap_ms
        if not intonation:
            self.opts.flags |= FLAG_NO_INTONATION

    @property
    def sample_rate(self):
        return self.opts.sample_rate

    def synth(self, ipa):
        """Return 16-bit little-endian mono PCM bytes."""
        lib = _lib()
        ptr = ctypes.POINTER(ctypes.c_int16)()
        n = ctypes.c_size_t()
        _check(lib.ipr_synth(ipa.encode(), ctypes.byref(self.voice), ctypes.byref(self.opts),
                             ctypes.byref(ptr), ctypes.byref(n)))
        try:
            return ctypes.string_at(ptr, n.value * 2)
        finally:
            lib.ipr_free(ptr)

    def synth_array(self, ipa):
        """Return a numpy int16 array (requires numpy)."""
        import numpy as np
        return np.frombuffer(self.synth(ipa), dtype=np.int16)

    def stream(self, ipa, callback):
        """Call callback(pcm_bytes) for each chunk while synthesizing. Return True from it to stop."""
        def cb(samples, n, _user):
            return 1 if callback(ctypes.string_at(samples, n * 2)) else 0
        c = _CB(cb)
        rc = _lib().ipr_synth_stream(ipa.encode(), ctypes.byref(self.voice), ctypes.byref(self.opts), c, None)
        if rc not in (0, -5):
            _check(rc)

    def wav(self, ipa):
        """Return a complete WAV file as bytes."""
        pcm = self.synth(ipa)
        lib = _lib()
        arr = (ctypes.c_int16 * (len(pcm) // 2)).from_buffer_copy(pcm)
        out = ctypes.POINTER(ctypes.c_uint8)()
        n = ctypes.c_size_t()
        _check(lib.ipr_wav_encode(arr, len(pcm) // 2, self.opts.sample_rate, ctypes.byref(out), ctypes.byref(n)))
        try:
            return ctypes.string_at(out, n.value)
        finally:
            lib.ipr_free(out)

    def save(self, ipa, path):
        with open(path, "wb") as f:
            f.write(self.wav(ipa))

    @staticmethod
    def unknown_count():
        return _lib().ipr_unknown_count()


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="Speak IPA text")
    ap.add_argument("text")
    ap.add_argument("-v", "--voice", default="male")
    ap.add_argument("-w", "--wav", default="out.wav")
    a = ap.parse_args()
    IPAReader(a.voice).save(a.text, a.wav)
    print("wrote", a.wav)
