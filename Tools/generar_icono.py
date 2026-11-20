#!/usr/bin/env python3
"""Dibuja el icono de la app (un mapa plegado en tres con un río, una ruta y una chincheta)
en los tamaños que piden el lanzador (assets/images) y la tienda (store/icon.png). Necesita
Pillow.

    Tools/generar_icono.py
"""

import math
import os

from PIL import Image, ImageDraw

LADO = 1024  # se dibuja grande y se reduce: así los bordes salen suaves


def recortar(capa, poligono):
    """Deja de `capa` sólo lo que cae dentro del polígono."""
    mascara = Image.new("L", (LADO, LADO), 0)
    ImageDraw.Draw(mascara).polygon(poligono, fill=255)
    vacia = Image.new("RGBA", (LADO, LADO), (0, 0, 0, 0))
    return Image.composite(capa, vacia, mascara)


def icono():
    imagen = Image.new("RGBA", (LADO, LADO), (0, 0, 0, 0))

    # Fondo: degradado azul petróleo, en un cuadrado de esquinas redondas.
    fondo = Image.new("RGBA", (LADO, LADO))
    trazo = ImageDraw.Draw(fondo)
    arriba, abajo = (32, 72, 88), (12, 28, 38)
    for y in range(LADO):
        t = y / (LADO - 1)
        trazo.line([(0, y), (LADO, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(arriba, abajo)) + (255,))
    mascara = Image.new("L", (LADO, LADO), 0)
    ImageDraw.Draw(mascara).rounded_rectangle([40, 40, LADO - 40, LADO - 40], radius=200, fill=255)
    imagen.paste(fondo, (0, 0), mascara)

    # El mapa: tres paneles en zigzag (plegado), el del medio más oscuro.
    xs = [170, 390, 634, 854]
    alto = 560
    tops = [300, 240, 300, 240]
    paneles = [
        [(xs[i], tops[i]), (xs[i + 1], tops[i + 1]), (xs[i + 1], tops[i + 1] + alto), (xs[i], tops[i] + alto)]
        for i in range(3)
    ]
    colores = [(196, 226, 170, 255), (150, 196, 132, 255), (196, 226, 170, 255)]

    # Lo que va «impreso» en el mapa se dibuja en una capa y se recorta a cada panel.
    dibujo = Image.new("RGBA", (LADO, LADO), (0, 0, 0, 0))
    d = ImageDraw.Draw(dibujo)
    # Curvas de nivel.
    for r, w in ((250, 10), (180, 10), (110, 10)):
        d.ellipse([300 - r, 640 - r * 0.7, 300 + r, 640 + r * 0.7], outline=(120, 160, 96, 255), width=w)
    # Río.
    rio = [(x, 420 + 60 * math.sin(x / 95.0)) for x in range(150, 880, 8)]
    d.line(rio, fill=(70, 150, 220, 255), width=34, joint="curve")
    # Ruta a trazos, de abajo a la izquierda hasta la chincheta.
    ruta = [(220 + t * 360, 820 - t * 300 - 60 * math.sin(t * math.pi)) for t in [i / 40 for i in range(41)]]
    for i in range(0, len(ruta) - 1, 2):
        d.line([ruta[i], ruta[i + 1]], fill=(232, 72, 85, 255), width=26)

    for poligono, color in zip(paneles, colores):
        panel = Image.new("RGBA", (LADO, LADO), (0, 0, 0, 0))
        ImageDraw.Draw(panel).polygon(poligono, fill=color)
        panel.alpha_composite(dibujo)
        imagen.alpha_composite(recortar(panel, poligono))
    # Sombra suave en los pliegues.
    trazo = ImageDraw.Draw(imagen)
    for x, top in ((xs[1], tops[1]), (xs[2], tops[2])):
        trazo.line([(x, top), (x, top + alto)], fill=(60, 90, 60, 160), width=8)

    # Chincheta: gota roja con el centro blanco.
    cx, cy, r = 600, 420, 130
    gota = [(cx - r * 0.86, cy + r * 0.5), (cx, cy + r * 2.15), (cx + r * 0.86, cy + r * 0.5)]
    sombra = Image.new("RGBA", (LADO, LADO), (0, 0, 0, 0))
    ds = ImageDraw.Draw(sombra)
    ds.ellipse([cx - 70, cy + r * 2.05 - 18, cx + 70, cy + r * 2.05 + 18], fill=(0, 0, 0, 90))
    imagen.alpha_composite(sombra)
    trazo.polygon(gota, fill=(222, 50, 64, 255))
    trazo.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(222, 50, 64, 255))
    trazo.ellipse([cx - 52, cy - 52, cx + 52, cy + 52], fill=(255, 255, 255, 255))
    return imagen


def main():
    raiz = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    imagen = icono()
    for ruta, lado in (
        ("assets/images/zerogps.png", 100),
        ("assets/images/zerogps_100.png", 100),
        ("assets/images/zerogps_80.png", 80),
        ("store/icon.png", 256),
    ):
        imagen.resize((lado, lado), Image.LANCZOS).save(os.path.join(raiz, ruta))


if __name__ == "__main__":
    main()
