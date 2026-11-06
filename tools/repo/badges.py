"""Saves the README badges to docs/img/badges/ as SVG files.

GitHub proxies external README images through its camo cache, which sometimes
times out on shields.io and shows a broken image. Keeping the static badges in
the repository avoids that. The version badges are a snapshot of the latest
release, so run this script again after publishing a new one.

Usage: python tools/repo/badges.py   (needs network access to img.shields.io)"""
import base64
import urllib.parse
import urllib.request
from pathlib import Path

root = Path(__file__).resolve().parents[2]
out = root / 'docs/img/badges'

# Simple Icons no longer ships the LinkedIn mark, so this is a plain "in" tile.
LINKEDIN = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">'
            '<rect x="1" y="1" width="22" height="22" rx="4" fill="#fff"/>'
            '<g fill="#0A66C2"><circle cx="7.2" cy="6.9" r="1.9"/>'
            '<rect x="5.5" y="9.6" width="3.4" height="9.6"/>'
            '<path d="M10.9 9.6h3.2v1.4c.6-1 1.8-1.7 3.3-1.7 2.5 0 3.6 1.6 3.6 4.3v5.6h-3.4v-5'
            'c0-1.3-.5-2.1-1.6-2.1-1.2 0-1.9.8-1.9 2.2v4.9h-3.2z"/></g></svg>')
linkedin = 'data:image/svg+xml;base64,' + base64.b64encode(LINKEDIN.encode()).decode()

DARK, BLUE = '1C1814', '0A66C2'
BADGES = {
    'board.svg': ('board', 'ESP32-S3 AMOLED 1.75"', DARK, {}),
    'placa.svg': ('placa', 'ESP32-S3 AMOLED 1.75"', DARK, {}),
    'android.svg': ('Android', '12+ ARM64', DARK, {}),
    'esp-idf.svg': ('ESP-IDF', '5.5.5', DARK, {}),
    'code-mit.svg': ('code', 'MIT', DARK, {}),
    'codigo-mit.svg': ('código', 'MIT', DARK, {}),
    'by-leo-candido.svg': ('by', 'Leo Candido', BLUE, {'logo': linkedin}),
    'por-leo-candido.svg': ('por', 'Leo Candido', BLUE, {'logo': linkedin}),
    'linkedin.svg': ('LinkedIn', 'Leo Candido', BLUE, {'logo': linkedin}),
    'github.svg': ('GitHub', 'Hellridergo', DARK, {'logo': 'github', 'logoColor': 'white'}),
}

# The version badges read the latest GitHub release. Run this again after each
# release so they show the new version.
RELEASE = 'https://img.shields.io/github/v/release/Hellridergo/meu-dot?color=F99D22&label='
LIVE = {
    'release.svg': RELEASE + 'release',
    'versao.svg': RELEASE + 'vers%C3%A3o',
}

def part(text):
    # shields.io static badge escaping: "-" becomes "--", "_" becomes "__".
    return urllib.parse.quote(text.replace('-', '--').replace('_', '__'), safe='')

urls = dict(LIVE)
for name, (label, message, color, extra) in BADGES.items():
    urls[name] = f'https://img.shields.io/badge/{part(label)}-{part(message)}-{color}'
    if extra:
        urls[name] += '?' + urllib.parse.urlencode(extra)

out.mkdir(parents=True, exist_ok=True)
for name, url in urls.items():
    req = urllib.request.Request(url, headers={'User-Agent': 'meu-dot-badges'})
    with urllib.request.urlopen(req, timeout=20) as r:
        svg = r.read()
    assert svg.startswith(b'<svg'), name
    (out / name).write_bytes(svg)
    print(name, len(svg), 'bytes')
