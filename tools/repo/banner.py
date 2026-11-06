"""Renders docs/img/banner.png (English) and docs/img/banner.pt-BR.png (Portuguese),
1280x640, also usable as the GitHub social preview.
Usage: python tools/repo/banner.py   (needs Pillow)"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter
root = Path(__file__).resolve().parents[2]
W, H = 1280, 640
INK, MUTED, AMBER = (255, 245, 232), (166, 154, 140), (249, 157, 34)
bg = Image.new('RGB', (W, H), (9, 8, 7))
cx, cy = 930, 320
halo = Image.new('L', (W, H), 0)
d = ImageDraw.Draw(halo)
for r in range(360, 0, -4):
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=int(255 * (1 - r / 360) ** 2.0))
halo = halo.filter(ImageFilter.GaussianBlur(30)).point(lambda v: int(v * 0.42))
bg = Image.composite(Image.new('RGB', (W, H), AMBER), bg, halo)
screen = Image.open(root / 'docs/img/screen-01-idle.png').convert('RGB')
size = 372
screen = screen.resize((size, size), Image.LANCZOS)
mask = Image.new('L', (size, size), 0)
ImageDraw.Draw(mask).ellipse((0, 0, size - 1, size - 1), fill=255)
bez = size + 26
bezel = Image.new('RGBA', (bez, bez), (0, 0, 0, 0))
bd = ImageDraw.Draw(bezel)
bd.ellipse((0, 0, bez - 1, bez - 1), fill=(46, 39, 32, 255))
bd.ellipse((5, 5, bez - 6, bez - 6), fill=(14, 12, 10, 255))
shadow = Image.new('L', (bez + 80, bez + 80), 0)
ImageDraw.Draw(shadow).ellipse((40, 54, bez + 40, bez + 54), fill=180)
bg.paste((0, 0, 0), (cx - bez // 2 - 40, cy - bez // 2 - 40), shadow.filter(ImageFilter.GaussianBlur(22)))
bg.paste(bezel, (cx - bez // 2, cy - bez // 2), bezel)
bg.paste(screen, (cx - size // 2, cy - size // 2), mask)
def font(size, weight):
    f = ImageFont.truetype(str(root / 'art/InterVariable.ttf'), size)
    f.set_variation_by_axes([min(32, max(14, size)), weight])
    return f
COPY = {
    'banner.png': (['A Dot that runs', 'in the palm', 'of your hand.'],
                   ['Talk to your ChatGPT Dot', 'on a round 1.75" AMOLED screen.']),
    'banner.pt-BR.png': (['Um Dot que roda', 'na palma', 'da sua mão.'],
                         ['Converse com o seu Dot do ChatGPT', 'numa tela AMOLED redonda de 1,75".']),
}
base = bg
for name, (title, sub) in COPY.items():
    bg = base.copy()
    dr = ImageDraw.Draw(bg)
    x = 88
    dr.ellipse((x, 106, x + 16, 122), fill=AMBER)
    dr.text((x + 28, 100), 'MEU DOT', font=font(20, 700), fill=AMBER)
    for i, line in enumerate(title):
        dr.text((x - 4, 136 + i * 76), line, font=font(68, 680), fill=INK)
    for i, line in enumerate(sub):
        dr.text((x, 398 + i * 34), line, font=font(25, 420), fill=MUTED)
    px, y = x, 502
    for chip in ['ESP32-S3 AMOLED 1.75"', 'Android', 'ChatGPT Sites + MCP']:
        ft = font(18, 560)
        tw = dr.textlength(chip, font=ft)
        dr.rounded_rectangle((px, y, px + tw + 32, y + 40), radius=20, outline=(62, 53, 44), width=2, fill=(21, 18, 15))
        dr.text((px + 16, y + 10), chip, font=ft, fill=INK)
        px += tw + 44
    for line in title + sub:
        f = font(68, 680) if line in title else font(25, 420)
        assert x + dr.textlength(line, font=f) < cx - 236, (name, line)
    bg.save(root / 'docs/img' / name, optimize=True)
