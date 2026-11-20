#!/usr/bin/env python3
"""Generate the supported-symbol tables in README.md from src/parse.c.

Run: python3 tools/gen_symbols.py   (rewrites the section between the markers in README.md)
"""
import re, os, unicodedata
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
src = open(os.path.join(ROOT, "src", "parse.c"), encoding="utf-8").read()

def cp(tok):
    tok = tok.strip()
    if tok.startswith("'"): return ord(tok[1])
    return int(tok, 16)

def block(name):
    m = re.search(r"static const \w+ %s\[\] = \{(.*?)\n\};" % name, src, re.S)
    return m.group(1)

def show(c):
    ch = chr(c)
    return ("◌" + ch) if unicodedata.combining(ch) else ch

HEIGHT = {"CL": "close", "NC": "near-close", "CM": "close-mid", "MD": "mid", "OM": "open-mid", "NO": "near-open", "OP": "open"}
def backness(b):
    b = float(b.rstrip("f"))
    return {0: "front", .2: "near-front", .5: "central", .55: "central (apical)", .8: "near-back", 1: "back"}.get(b, str(b))

out = []
out.append("### Vowels\n\n| Symbol | Code point | Description |\n|---|---|---|")
for m in re.finditer(r"\{(0x[0-9A-F]+|'.'),\s*(\w+),\s*([\d.f]+),\s*([\d.f]+),\s*([\d.f]+)\}", block("VOWELS")):
    c = cp(m.group(1)); h = HEIGHT.get(m.group(2), m.group(2)); r = "rounded" if m.group(4).startswith("1") else "unrounded"
    rh = " r-coloured" if float(m.group(5).rstrip("f")) > 0 else ""
    out.append(f"| {show(c)} | U+{c:04X} | {h} {backness(m.group(3))} {r}{rh} |")

PL = {"P_BILABIAL": "bilabial", "P_LABIODENTAL": "labiodental", "P_DENTAL": "dental", "P_ALVEOLAR": "alveolar",
      "P_POSTALV": "postalveolar", "P_RETROFLEX": "retroflex", "P_ALVPAL": "alveolo-palatal", "P_PALATAL": "palatal",
      "P_VELAR": "velar", "P_UVULAR": "uvular", "P_PHARYNGEAL": "pharyngeal", "P_EPIGLOTTAL": "epiglottal",
      "P_GLOTTAL": "glottal", "P_LABVELAR": "labial-velar", "P_LABPAL": "labial-palatal"}
MN = {"M_PLOSIVE": "plosive", "M_NASAL": "nasal", "M_TRILL": "trill", "M_TAP": "tap/flap", "M_FRIC": "fricative",
      "M_LATFRIC": "lateral fricative", "M_APPROX": "approximant", "M_LATAPPROX": "lateral approximant",
      "M_LATTAP": "lateral flap", "M_IMPLOSIVE": "implosive", "M_CLICK": "click"}
out.append("\n### Consonants\n\n| Symbol | Code point | Description |\n|---|---|---|")
for m in re.finditer(r"\{(0x[0-9A-F]+|'.'),\s*(P_\w+),\s*(M_\w+),\s*(\d),\s*(\d)\}", block("CONS")):
    c = cp(m.group(1))
    if c == ord('!'): continue
    v = "voiced" if m.group(4) == "1" else "voiceless"
    mn = MN[m.group(3)]
    if mn == "click":
        v = ""; mn = ("lateral " if m.group(5) == "1" else "") + "click"
    elif m.group(5) == "1": mn = "sibilant " + mn
    extra = " (velarized, dark l)" if c == 0x026B else ""
    if c == ord('g'): extra = " (ASCII g, same as ɡ)"
    out.append(f"| {show(c)} | U+{c:04X} | {v} {PL[m.group(2)]} {mn}{extra} |".replace("|  ", "| "))

out.append("\n### Ligatures, precomposed letters and accepted look-alikes\n\nThese are rewritten to the IPA sequence shown before analysis.\n\n| Input | Code point | Read as |\n|---|---|---|")
for m in re.finditer(r"\{(0x[0-9A-F]+),\s*\{([^}]*)\}\}", block("DECOMP")):
    c = cp(m.group(1)); seq = [cp(t) for t in m.group(2).split(",") if t.strip() and t.strip() != "0"]
    if not seq or seq == [c]: continue
    target = "".join(chr(x) for x in seq)
    out.append(f"| {chr(c)} | U+{c:04X} | {target} |")

out.append("\n### Superscript letters\n\nA superscript letter after a sound is pronounced as a short release of that sound (e.g. tˢ, kˣ, dᶻ, tᵊ). "
           "A superscript nasal before a consonant prenasalizes it (ᵐb, ⁿd, ᵑɡ); ⁿ and ˡ after a plosive give nasal / lateral release. "
           "ʰ ʱ ʷ ʲ ˠ ˤ are listed under diacritics.\n\n| Symbol | Code point | Short |\n|---|---|---|")
for m in re.finditer(r"\{(0x[0-9A-F]+),\s*(0x[0-9A-F]+|'.')\}", block("SUPS")):
    c, b = cp(m.group(1)), cp(m.group(2))
    out.append(f"| {chr(c)} | U+{c:04X} | {chr(b)} |")

DIA = [
 (0x02B0, "aspirated (after a plosive); preaspiration if written before a consonant"),
 (0x02B1, "breathy-voiced aspiration"),
 (0x02B7, "labialized"), (0x02B2, "palatalized"), (0x02E0, "velarized"), (0x02E4, "pharyngealized"),
 (0x02C1, "pharyngealized (alternative)"), (0x02C0, "glottalized"), (0x1DA3, "labial-palatalized"),
 (0x207F, "nasal release (after plosive) / prenasalization (before consonant)"), (0x02E1, "lateral release"),
 (0x02BC, "ejective (also ' after a consonant, and ’)"), (0x02DE, "rhoticity (r-colouring)"),
 (0x0325, "voiceless"), (0x030A, "voiceless (above)"), (0x032C, "voiced"), (0x0324, "breathy voiced"),
 (0x0330, "creaky voiced"), (0x0303, "nasalized"), (0x0328, "nasalized (ogonek, Americanist)"),
 (0x0329, "syllabic"), (0x030D, "syllabic (above)"), (0x032F, "non-syllabic"), (0x0311, "non-syllabic (above)"),
 (0x031F, "advanced"), (0x0320, "retracted"), (0x0308, "centralized"), (0x033D, "mid-centralized"),
 (0x031D, "raised (approximant → fricative)"), (0x031E, "lowered (fricative → approximant)"),
 (0x0339, "more rounded"), (0x031C, "less rounded"), (0x0318, "advanced tongue root"), (0x0319, "retracted tongue root"),
 (0x032A, "dental"), (0x033C, "linguolabial"), (0x033A, "apical (accepted, no audible change)"),
 (0x033B, "laminal (accepted, no audible change)"), (0x031A, "no audible release"), (0x0334, "velarized or pharyngealized"),
 (0x0322, "retroflex / r-coloured"), (0x0323, "retroflex (Indological dot below)"), (0x0306, "extra-short"),
 (0x0361, "tie bar: affricates t͡s, double articulation k͡p, prenasalized m͡b, diphthongs a͡i"), (0x035C, "tie bar (below)"),
]
SUPRA = [
 (0x02C8, "primary stress (also ASCII ' before a syllable)"), (0x02CC, "secondary stress"),
 (0x02D0, "long (also ASCII : after a sound)"), (0x02D1, "half-long"), (0x002E, "syllable break (end of sentence when followed by space)"),
 (0x007C, "minor (foot) group pause"), (0x2016, "major (intonation) group pause (also ||)"),
 (0x203F, "linking (no break)"), (0xA71B, "upstep"), (0xA71C, "downstep"),
 (0x2197, "global rise (accepted)"), (0x2198, "global fall (accepted)"),
 (0x002C, "short pause with continuation rise"), (0x003F, "question intonation"), (0x0021, "exclamation (inside a word: alveolar click ǃ)"),
 (0x005F, "short pause"), (0x003B, "pause"),
]
TONES = [
 (0x02E5, "tone letter extra high (5)"), (0x02E6, "tone letter high (4)"), (0x02E7, "tone letter mid (3)"),
 (0x02E8, "tone letter low (2)"), (0x02E9, "tone letter extra low (1)"),
 (0x030B, "extra-high tone"), (0x0301, "high tone"), (0x0304, "mid tone"), (0x0300, "low tone"), (0x030F, "extra-low tone"),
 (0x030C, "rising tone"), (0x0302, "falling tone"), (0x1DC4, "high (mid-high) rising"), (0x1DC5, "low rising"),
 (0x1DC6, "low falling"), (0x1DC7, "high falling"), (0x1DC8, "rising-falling"), (0x1DC9, "falling-rising"),
 (0x00B9, "Chao tone digit 1 (also ²³⁴⁵ and ASCII 1–5 after a syllable)"),
]
for title, rows in (("Diacritics and modifier letters", DIA), ("Suprasegmentals and punctuation", SUPRA), ("Tones", TONES)):
    out.append(f"\n### {title}\n\n| Symbol | Code point | Meaning |\n|---|---|---|")
    for c, d in rows:
        lit = "0x%04X" % c
        assert lit in src or ("'%s'" % chr(c)) in src or c in (0x2016, 0x02E6, 0x02E7, 0x02E8), "not in parse.c: " + lit
        s = show(c).replace("|", "\\|")
        out.append(f"| {s} | U+{c:04X} | {d} |")
out.append("\nContour tones are written as sequences of tone letters (˧˥, ˨˩˦, ˥˩) or digits (ma⁵¹, ma51). "
           "Tone letters A708–A716 (dotted / left-stem forms) are read as the same five levels.")

text = "\n".join(out) + "\n"
readme = os.path.join(ROOT, "README.md")
r = open(readme, encoding="utf-8").read()
a, b = "<!-- SYMBOLS:BEGIN -->", "<!-- SYMBOLS:END -->"
r = r[: r.index(a) + len(a)] + "\n" + text + r[r.index(b):]
open(readme, "w", encoding="utf-8").write(r)
print("updated README.md:", len(out), "lines")
