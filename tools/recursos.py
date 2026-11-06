"""Converte o atlas âmbar de 27 quadros, os ícones e a Inter (OFL) para o visor.

Cada pose vira RGB565 pré-multiplicado (composto sobre preto) mais uma máscara
A4, para o compositor desenhar o Dot com transparência real sobre o halo. Os
tamanhos menores (78% e 60%) saem prontos daqui, filtrados em Lanczos.
Os ícones são desenhados em 4x e reduzidos para A4, com bordas suaves.
Uso: python tools/recursos.py  (gera interface/art_data.inc)
"""
from pathlib import Path
from math import sin, cos, atan2, pi, sqrt
from PIL import Image, ImageFont, ImageDraw

root = Path(__file__).resolve().parent.parent
sheet = Image.open(root / 'art/quadros-ambar.png').convert('RGBA')
frame_size = (192, 170)
columns = 9
rows = 3
if sheet.size != (frame_size[0] * columns, frame_size[1] * rows):
    raise ValueError('Expected the nine-column, three-row amber expression atlas')
path = root / 'interface/art_data.inc'
FONTS = [('tiny', 14, 550), ('micro', 13, 660), ('small', 18, 470), ('strong', 18, 600),
         ('body', 23, 430), ('headline', 34, 620), ('display', 54, 600)]
S = 4  # superamostragem dos ícones


def values(out, typename, name, array, count=20):
    out.write(f'static const {typename} {name}[] = {{\n')
    for start in range(0, len(array), count):
        out.write(','.join(map(str, array[start:start + count])) + ',\n')
    out.write('};\n')


def pack_a4(alpha):
    alpha = [(a + 8) // 17 for a in alpha]
    if len(alpha) % 2:
        alpha.append(0)
    return [(a << 4) | b for a, b in zip(alpha[::2], alpha[1::2])]


def rounded_poly(corners, radius, steps=10):
    """Contorno de um polígono convexo com cantos arredondados."""
    pts = []
    n = len(corners)
    for i in range(n):
        p0, p1, p2 = corners[i - 1], corners[i], corners[(i + 1) % n]
        a = atan2(p0[1] - p1[1], p0[0] - p1[0]); b = atan2(p2[1] - p1[1], p2[0] - p1[0])
        half = ((b - a) % (2 * pi)); half = min(half, 2 * pi - half) / 2
        bis = a + ((b - a) % (2 * pi)) / 2
        if (b - a) % (2 * pi) > pi:
            bis += pi
        dist = radius / sin(half)
        cx, cy = p1[0] + dist * cos(bis), p1[1] + dist * sin(bis)
        start = a + pi / 2 if True else 0
        # Varre o arco entre as normais das duas arestas.
        na = atan2(p1[1] - cy, p1[0] - cx)
        span = pi - 2 * half
        for k in range(steps + 1):
            t = na - span / 2 + span * k / steps
            pts.append((cx + radius * cos(t), cy + radius * sin(t)))
    return pts


def icon_images():
    """Ícones de traço arredondado, no tamanho final da tela."""
    icons = []

    def canvas(w, h):
        im = Image.new('L', (w * S, h * S), 0)
        return im, ImageDraw.Draw(im)

    def stroke(d, pts, width, closed=False):
        pts = [(x * S, y * S) for x, y in pts]
        if closed:
            pts = pts + pts[:2]
        d.line(pts, fill=255, width=round(width * S), joint='curve')
        r = width * S / 2
        for x, y in pts:
            d.ellipse((x - r, y - r, x + r, y + r), fill=255)

    def ring(d, cx, cy, r, width):
        d.ellipse(((cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S), outline=255, width=round(width * S))

    # 0 Bluetooth
    im, d = canvas(12, 20)
    stroke(d, [(1.6, 5.8), (10.4, 14.2), (6, 18.4), (6, 1.6), (10.4, 5.8), (1.6, 14.2)], 1.8)
    icons.append(im)
    # 1 Ajustes (dois controles deslizantes)
    im, d = canvas(24, 22)
    stroke(d, [(2.5, 6), (4.4, 6)], 1.8); stroke(d, [(11.6, 6), (21.5, 6)], 1.8)
    stroke(d, [(2.5, 16), (12.4, 16)], 1.8); stroke(d, [(19.6, 16), (21.5, 16)], 1.8)
    ring(d, 8, 6, 3.3, 1.8); ring(d, 16, 16, 3.3, 1.8)
    icons.append(im)
    # 2 Microfone
    im, d = canvas(18, 22)
    d.rounded_rectangle((5.5 * S, 1 * S, 12.5 * S, 13 * S), radius=3.5 * S, fill=255)
    d.arc((2 * S, 3 * S, 16 * S, 17 * S), 0, 180, fill=255, width=2 * S)
    stroke(d, [(9, 17), (9, 20.4)], 2); stroke(d, [(5.8, 20.4), (12.2, 20.4)], 2)
    icons.append(im)
    # 3 Contorno da bateria (o nível é preenchido em tempo de execução)
    im, d = canvas(28, 14)
    d.rounded_rectangle((0.8 * S, 0.8 * S, 24.2 * S, 13.2 * S), radius=4 * S, outline=255, width=round(1.6 * S))
    d.rounded_rectangle((25.2 * S, 4.5 * S, 27.4 * S, 9.5 * S), radius=1 * S, fill=255)
    icons.append(im)
    # 4 Raio (carregando)
    im, d = canvas(10, 14)
    d.polygon([(6.4 * S, 0.4 * S), (0.8 * S, 8 * S), (4.6 * S, 8 * S), (3.6 * S, 13.6 * S), (9.2 * S, 5.8 * S), (5.4 * S, 5.8 * S)], fill=255)
    icons.append(im)
    # 5 Seta para a direita
    im, d = canvas(9, 15)
    stroke(d, [(1.8, 1.8), (7.2, 7.5), (1.8, 13.2)], 2)
    icons.append(im)
    # 6 Placa redonda
    im, d = canvas(24, 24)
    ring(d, 12, 12, 9, 1.8)
    icons.append(im)
    # 7 Celular
    im, d = canvas(16, 24)
    d.rounded_rectangle((1.4 * S, 1.4 * S, 14.6 * S, 22.6 * S), radius=3.6 * S, outline=255, width=round(1.8 * S))
    stroke(d, [(6, 19), (10, 19)], 1.8)
    icons.append(im)
    # 8 Silhueta do Dot (triângulo de cantos bem arredondados)
    im, d = canvas(24, 22)
    stroke(d, rounded_poly([(12, 1.5), (22.5, 20.5), (1.5, 20.5)], 3.6), 1.8, closed=True)
    icons.append(im)
    # 9 Visto
    im, d = canvas(16, 12)
    stroke(d, [(1.8, 6.4), (5.8, 10.2), (14.2, 1.8)], 2)
    icons.append(im)
    return [im.resize((im.width // S, im.height // S), Image.LANCZOS) for im in icons]


with path.open('w') as out:
    out.write('/* Generated by tools/recursos.py from the amber poses, icons and Inter. */\n')
    for tag, size, weight in FONTS:
        font = ImageFont.truetype(str(root / 'art/InterVariable.ttf'), size)
        font.set_variation_by_axes([min(32, max(14, size)), weight])
        glyphs = []; pixels = []
        for code in range(32, 256):
            s = chr(code) if code < 127 or code >= 160 else '?'
            x, y, r, b = font.getbbox(s, anchor='ls')
            w = r - x; h = b - y
            glyphs.append((len(pixels), w, h, round(font.getlength(s)), x, y))
            if w and h:
                im = Image.new('L', (w, h))
                ImageDraw.Draw(im).text((-x, -y), s, font=font, fill=255, anchor='ls')
                pixels.extend(pack_a4(list(im.tobytes())))
        values(out, 'uint8_t', tag + '_alpha', pixels)
        values(out, 'Glyph', tag + '_glyph', ['{' + ','.join(map(str, g)) + '}' for g in glyphs], 7)
        out.write(f'static const Font {tag} = {{{tag}_glyph,{tag}_alpha}};\n')
    glyphs = []; pixels = []
    for im in icon_images():
        glyphs.append((len(pixels), im.width, im.height, im.width, 0, 0))
        pixels.extend(pack_a4(list(im.tobytes())))
    values(out, 'uint8_t', 'icon_alpha', pixels)
    values(out, 'Glyph', 'icon_glyph', ['{' + ','.join(map(str, g)) + '}' for g in glyphs], 7)
    out.write('static const Font icons = {icon_glyph,icon_alpha};\n')
    cw, ch = frame_size
    for i in range(columns * rows):
        x, y = (i % columns) * cw, (i // columns) * ch
        src = sheet.crop((x, y, x + cw, y + ch))
        a4 = [(a + 8) // 17 for a in src.getchannel('A').tobytes()]
        data = []
        for (r, g, b, _), a in zip(zip(*[iter(src.tobytes())] * 4), a4):
            # Pré-multiplicado pela mesma alfa de 4 bits usada na tela.
            r, g, b = (r * a + 7) // 15, (g * a + 7) // 15, (b * a + 7) // 15
            data.append(((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255))
        values(out, 'uint16_t', f'pose{i}', data)
        values(out, 'uint8_t', f'pose{i}_alpha', pack_a4([a * 17 for a in a4]))
    refs = ','.join(f'{{pose{i},pose{i}_alpha,{cw},{ch}}}' for i in range(columns * rows))
    out.write(f'static const Sprite poses[]={{{refs}}};\n')
    # Smaller sizes, already filtered here so the badge only copies pixels. Only
    # the poses those sizes use: blink 00-08 and speech 18-26.
    for tag, scale in (('m', 200), ('s', 154)):
        w, h = cw * scale // 256, ch * scale // 256
        refs = []
        for k, i in enumerate(list(range(9)) + list(range(18, 27))):
            x, y = (i % columns) * cw, (i // columns) * ch
            src = sheet.crop((x, y, x + cw, y + ch)).convert('RGBa').resize((w, h), Image.LANCZOS).convert('RGBA')
            a4 = [(a + 8) // 17 for a in src.getchannel('A').tobytes()]
            data = []
            for (r, g, b, _), a in zip(zip(*[iter(src.tobytes())] * 4), a4):
                r, g, b = (r * a + 7) // 15, (g * a + 7) // 15, (b * a + 7) // 15
                data.append(((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255))
            values(out, 'uint16_t', f'pose_{tag}{k}', data)
            values(out, 'uint8_t', f'pose_{tag}{k}_alpha', pack_a4([a * 17 for a in a4]))
            refs.append(f'{{pose_{tag}{k},pose_{tag}{k}_alpha,{w},{h}}}')
        out.write(f'static const Sprite poses_{tag}[]={{{",".join(refs)}}};\n')
    values(out, 'int16_t', 'sine', [round(sin(i * 2 * pi / 256) * 256) for i in range(256)])
    # Queda do halo pela distância ao quadrado: (1 - d/R)^1.7, de 0 a 255.
    values(out, 'uint8_t', 'halo_curve', [round(255 * (1 - sqrt(i / 256)) ** 1.7) for i in range(257)])
print('ok', path, path.stat().st_size // 1024, 'KB')
