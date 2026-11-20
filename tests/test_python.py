import os, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "python"))
import ipareader

assert ipareader.builtin_voices() == ["male", "female"]
for v in ["male", "female", os.path.join(os.path.dirname(__file__), "..", "voices", "child.voice"),
          {"base": "female", "pitch": 230}, "base = male\npitch = 90\n"]:
    t = ipareader.IPAReader(voice=v)
    pcm = t.synth("ˈhɛloʊ ˈwɝld")
    assert len(pcm) > 20000, len(pcm)
wav = ipareader.IPAReader("female", sample_rate=16000).wav("ni˧˥ xɑʊ˨˩˦")
assert wav[:4] == b"RIFF" and len(wav) > 44
chunks = []
ipareader.IPAReader().stream("ˈbɑnʒuʁ", lambda b: chunks.append(b))
assert sum(map(len, chunks)) > 2000
with tempfile.TemporaryDirectory() as d:
    p = os.path.join(d, "x.wav")
    ipareader.IPAReader().save("ǃa ǀa ǁa", p)
    assert os.path.getsize(p) > 1000
assert "click" in ipareader.analyze("ǃa")
try:
    ipareader.IPAReader(voice="nonexistent-voice-file")
    raise SystemExit("expected error")
except ipareader.IPAReaderError:
    pass
print("all Python tests passed")
