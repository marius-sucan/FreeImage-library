#!/usr/bin/env python3
"""Fetch the JPEG XL conformance suite (BSD-3-Clause, github.com/libjxl/conformance) into conformance/:
input.jxl from git, the reference arrays and profiles from its public bucket, checked by SHA-256."""
import json, os, sys, urllib.request, hashlib

DST = sys.argv[1] if len(sys.argv) > 1 else 'conformance'
RAW = 'https://raw.githubusercontent.com/libjxl/conformance/master/testcases/'
API = 'https://api.github.com/repos/libjxl/conformance/contents/testcases?ref=master'
BLOB = 'https://storage.googleapis.com/jxl-conformance/objects/'

def get(url):
    with urllib.request.urlopen(url, timeout=120) as r:
        return r.read()

cases = [e['name'] for e in json.loads(get(API)) if e['type'] == 'dir']
for c in cases:
    d = os.path.join(DST, c)
    os.makedirs(d, exist_ok=True)
    tj = get(RAW + c + '/test.json')
    open(os.path.join(d, 'test.json'), 'wb').write(tj)
    if not os.path.exists(os.path.join(d, 'input.jxl')) or os.path.getsize(os.path.join(d, 'input.jxl')) < 64:
        data = get(RAW + c + '/input.jxl')
        if len(data) < 64 and data.startswith(b'../'):
            # a git symlink: its text is the target path
            data = get(RAW + c + '/' + data.decode())
        open(os.path.join(d, 'input.jxl'), 'wb').write(data)
    for name, sha in json.loads(tj).get('sha256sums', {}).items():
        p = os.path.join(d, name)
        if os.path.exists(p) and hashlib.sha256(open(p, 'rb').read()).hexdigest() == sha:
            continue
        data = get(BLOB + sha)
        assert hashlib.sha256(data).hexdigest() == sha, (c, name)
        open(p, 'wb').write(data)
    print(c, sorted(os.listdir(d)))
