#!/usr/bin/env python3
"""Intelligibility benchmark for IPAReader using an automatic speech recogniser as the listener.

Every test sentence is synthesized from IPA, transcribed by Whisper (via sherpa-onnx), and the
transcript is compared with the original sentence:
  * English: word error rate (WER) over 102 sentences
  * 13 other languages: character error rate (CER) over 161 sentences

Setup (one time):
    pip install sherpa-onnx numpy opencc-python-reimplemented
    curl -LO https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-whisper-small.tar.bz2
    tar xjf sherpa-onnx-whisper-small.tar.bz2

Run:
    python3 tools/eval/asr_eval.py --model sherpa-onnx-whisper-small --voice male
    python3 tools/eval/asr_eval.py --model ... --voice female --set english -v

Lower is better. The IPA for most sentences comes from espeak-ng's text-to-IPA (only used to make
test data), so a few transcriptions are themselves imperfect.
"""
import argparse, json, os, re, subprocess, sys, unicodedata
from concurrent.futures import ThreadPoolExecutor
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
NUM = {'0': 'zero', '1': 'one', '2': 'two', '3': 'three', '4': 'four', '5': 'five', '6': 'six', '7': 'seven',
       '8': 'eight', '9': 'nine', '10': 'ten', '50': 'fifty', '60': 'sixty', '8am': 'eight', '9am': 'nine'}


def edit(r, h):
    d = np.zeros((len(r) + 1, len(h) + 1)); d[:, 0] = range(len(r) + 1); d[0, :] = range(len(h) + 1)
    for i in range(1, len(r) + 1):
        for j in range(1, len(h) + 1):
            d[i, j] = min(d[i - 1, j] + 1, d[i, j - 1] + 1, d[i - 1, j - 1] + (r[i - 1] != h[j - 1]))
    return d[-1, -1]


def words(s):
    s = re.sub(r"[.,!?]", " ", s.lower().replace('-', ' '))
    return [NUM.get(w, w) for w in re.sub(r"[^a-z0-9' ]", "", s).split()]


def chars(s, cc):
    s = unicodedata.normalize('NFC', cc.convert(s).lower() if cc else s.lower())
    return ''.join(ch for ch in s if unicodedata.category(ch)[0] in 'LN')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--model', required=True, help='directory of sherpa-onnx-whisper-small')
    ap.add_argument('--voice', default='male')
    ap.add_argument('--bin', default=os.path.join(HERE, '..', '..', 'build', 'ipareader'))
    ap.add_argument('--set', choices=['english', 'multilingual', 'both'], default='both')
    ap.add_argument('-v', action='store_true', help='print every sentence with errors')
    a = ap.parse_args()
    import sherpa_onnx
    try:
        from opencc import OpenCC
        cc = OpenCC('t2s')
    except ImportError:
        cc = None
    data = json.load(open(os.path.join(HERE, 'testset.json'), encoding='utf-8'))
    m = os.path.join(a.model, 'small-')
    recs = {}

    def transcribe(lang, pcm):
        if lang not in recs:
            recs.clear()
            recs[lang] = sherpa_onnx.OfflineRecognizer.from_whisper(
                encoder=m + 'encoder.int8.onnx', decoder=m + 'decoder.int8.onnx', tokens=m + 'tokens.txt',
                language=lang, task='transcribe', num_threads=4)
        r = recs[lang]
        x = np.frombuffer(pcm, np.int16).astype(np.float32) / 32768
        x = np.concatenate([np.zeros(4000, np.float32), x, np.zeros(8000, np.float32)])
        s = r.create_stream(); s.set_option('max_tokens_per_second', '40')
        s.accept_waveform(16000, x); r.decode_stream(s)
        return s.result.text

    def synth(ipa):
        return subprocess.run([a.bin, '-v', a.voice, '-r', '16000', '--raw', ipa],
                              capture_output=True, check=True).stdout

    if a.set in ('english', 'both'):
        items = data['english']
        pcms = list(ThreadPoolExecutor(4).map(lambda d: synth(d['ipa']), items))
        E = N = 0
        for d, p in zip(items, pcms):
            h = transcribe('en', p); r = words(d['text']); e = min(edit(r, words(h)), len(r)); E += e; N += len(r)
            if a.v and e: print(f'  [{int(e)}] {d["ipa"]}\n      {d["text"]} -> {h}')
        print(f'English WER: {100 * E / N:.1f}%  ({len(items)} sentences, voice {a.voice})')
    if a.set in ('multilingual', 'both'):
        items = sorted(data['multilingual'], key=lambda d: d['lang'])
        pcms = list(ThreadPoolExecutor(4).map(lambda d: synth(d['ipa']), items))
        tot = {}
        for d, p in zip(items, pcms):
            h = transcribe(d['lang'], p); r = chars(d['text'], cc); e = min(edit(r, chars(h, cc)), len(r))
            t = tot.setdefault(d['lang'], [0, 0]); t[0] += e; t[1] += len(r)
            if a.v and e / max(len(r), 1) > 0.2: print(f'  {d["lang"]} {d["ipa"]}\n      {d["text"]} -> {h}')
        E = sum(t[0] for t in tot.values()); N = sum(t[1] for t in tot.values())
        print(f'Multilingual CER: {100 * E / N:.1f}%  ' + ' '.join(f'{l}:{100 * t[0] / t[1]:.0f}' for l, t in tot.items()))


if __name__ == '__main__':
    main()
