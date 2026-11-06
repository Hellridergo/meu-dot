"""Recolore o Dot azul original para o âmbar de referência #F99D22.

Só a pelagem muda. Óculos, gravata, olhos, boca e transparência ficam como no
desenho original. A troca acontece em OKLab: a luminosidade de cada pelo é
medida em relação ao azul mediano e reaplicada sobre o âmbar, com sombras um
pouco mais quentes e brilhos um pouco mais dourados, como numa pelúcia real.
Uso: python tools/ambar.py  (gera art/quadros-ambar.png e art/personagem.png)
"""
from pathlib import Path
import numpy as np
from PIL import Image

root = Path(__file__).resolve().parent.parent
TARGET = (0xF9, 0x9D, 0x22)


def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


M1 = np.array([[0.4122214708, 0.5363325363, 0.0514459929],
               [0.2119034982, 0.6806995451, 0.1073969566],
               [0.0883024619, 0.2817188376, 0.6299787005]])
M2 = np.array([[0.2104542553, 0.7936177850, -0.0040720468],
               [1.9779984951, -2.4285922050, 0.4505937099],
               [0.0259040371, 0.7827717662, -0.8086757660]])


def to_oklab(rgb):
    lms = srgb_to_linear(rgb) @ M1.T
    return np.cbrt(lms) @ M2.T


def from_oklab(lab):
    lms = (lab @ np.linalg.inv(M2).T) ** 3
    return linear_to_srgb(lms @ np.linalg.inv(M1).T)


def in_gamut(lab):
    lms = (lab @ np.linalg.inv(M2).T) ** 3
    lin = lms @ np.linalg.inv(M1).T
    return np.all((lin >= -1e-4) & (lin <= 1 + 1e-4), axis=-1)


def recolor(path_in, path_out):
    im = np.asarray(Image.open(path_in).convert('RGBA')).astype(np.float64) / 255
    rgb, alpha = im[..., :3], im[..., 3:]
    lab = to_oklab(rgb)
    L, a, b = lab[..., 0], lab[..., 1], lab[..., 2]
    C = np.hypot(a, b)
    h = np.degrees(np.arctan2(b, a)) % 360
    fur = (alpha[..., 0] > 0.9) & (C > 0.08)
    href = np.median(h[fur]); Lref = np.median(L[fur]); Cref = np.median(C[fur])
    t = to_oklab(np.array(TARGET, dtype=np.float64) / 255)
    Lt, Ct = t[0], np.hypot(t[1], t[2])
    ht = np.degrees(np.arctan2(t[2], t[1])) % 360
    # Peso: pelos de matiz azul e com croma; preto, branco e creme ficam fora.
    dh = np.abs((h - href + 180) % 360 - 180)
    w_h = np.clip((70 - dh) / 30, 0, 1)
    w_c = np.clip((C - 0.012) / 0.03, 0, 1)
    w = (w_h * w_c)[..., None]
    ratio = np.clip(L / Lref, 0, 4)
    L2 = Lt * ratio ** 1.15
    L2 = np.clip(L2, 0, 0.975)
    C2 = C * (Ct / Cref)
    # Sombras puxam para o laranja-queimado e brilhos para o dourado.
    h2 = ht + (L2 - Lt) * 38 + ((h - href + 180) % 360 - 180) * 0.35
    rad = np.radians(h2)
    new = np.stack([L2, C2 * np.cos(rad), C2 * np.sin(rad)], -1)
    # Reduz o croma até caber em sRGB, sem mudar luminosidade nem matiz.
    for _ in range(24):
        bad = ~in_gamut(new)
        if not bad.any():
            break
        new[bad, 1:] *= 0.93
    mixed = lab * (1 - w) + new * w
    out = from_oklab(mixed)
    result = np.concatenate([out, alpha], -1)
    Image.fromarray(np.round(result * 255).astype(np.uint8), 'RGBA').save(path_out, optimize=True)
    fur_after = np.round(from_oklab(new[fur]) * 255)
    print(path_out.name, 'mediana da pelagem', np.median(fur_after, 0).astype(int).tolist(), 'alvo', list(TARGET))


if __name__ == '__main__':
    art = root / 'art'
    recolor(art / 'quadros-azuis.png', art / 'quadros-ambar.png')
    source = art / 'personagem-azul.png'
    if not source.exists():
        (art / 'personagem.png').rename(source)
    recolor(source, art / 'personagem.png')
