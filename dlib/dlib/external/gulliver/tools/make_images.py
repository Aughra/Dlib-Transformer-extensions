# Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
# License: Boost Software License   See LICENSE.txt for the full license.
#
# Produit les illustrations d'Albireo (sources SVG dans web/img et resources/art) dans les fichiers
# que les programmes embarquent : les icônes des programmes et de la zone de notification (ICO),
# les images de l'installateur et des fenêtres de la zone de notification (PNG), et les icônes PNG
# de l'application web.  Les sorties sont gardées dans le dépôt, pour qu'un build n'ait besoin ni
# d'un navigateur ni de Python ; lancer ce script après un changement des illustrations :
#
#   python3 tools/make_images.py
#
# Il a besoin des paquets Python playwright (avec son Chromium) et Pillow.

import asyncio
import io
import os

from PIL import Image
from playwright.async_api import async_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
WEB = os.path.join(ROOT, 'web', 'img')
ART = os.path.join(ROOT, 'resources', 'art')
OUT = os.path.join(ROOT, 'resources')


async def render(page, svg_path, width, height, background='transparent'):
    svg = open(svg_path, encoding='utf-8').read()
    await page.set_viewport_size({'width': width, 'height': height})
    await page.set_content('<html><body style="margin:0;background:%s"><div style="width:%dpx;height:%dpx">%s</div>'
                           '<style>svg{width:%dpx;height:%dpx;display:block}</style></body></html>'
                           % (background, width, height, svg, width, height))
    png = await page.screenshot(omit_background=(background == 'transparent'),
                                clip={'x': 0, 'y': 0, 'width': width, 'height': height})
    return Image.open(io.BytesIO(png)).convert('RGBA')


async def icon(page, svg_path, out_path, sizes):
    images = [await render(page, svg_path, s, s) for s in sizes]
    largest = images[-1]
    largest.save(out_path, format='ICO', sizes=[(s, s) for s in sizes], append_images=images[:-1])


async def main():
    async with async_playwright() as p:
        browser = await p.chromium.launch()
        page = await browser.new_page(device_scale_factor=1)
        app_sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
        tray_sizes = [16, 20, 24, 32, 40, 48, 64]
        await icon(page, os.path.join(WEB, 'albireo-icon.svg'), os.path.join(OUT, 'albireo.ico'), app_sizes)
        for name in ('tray', 'tray-busy', 'tray-off', 'tray-update'):
            await icon(page, os.path.join(ART, name + '.svg'), os.path.join(OUT, name + '.ico'), tray_sizes)
        # Images de l'installateur et des fenêtres, au double de leur taille à 96 DPI.
        (await render(page, os.path.join(ART, 'setup-side.svg'), 480, 1040)).save(os.path.join(OUT, 'setup-side.png'))
        (await render(page, os.path.join(WEB, 'about.svg'), 1440, 440)).save(os.path.join(OUT, 'about.png'))
        (await render(page, os.path.join(WEB, 'albireo-mark.svg'), 128, 128)).save(os.path.join(OUT, 'mark.png'))
        for s in (192, 512):
            image = await render(page, os.path.join(WEB, 'albireo-icon.svg'), s, s)
            image.save(os.path.join(WEB, 'albireo-%d.png' % s))
        await browser.close()


asyncio.run(main())
