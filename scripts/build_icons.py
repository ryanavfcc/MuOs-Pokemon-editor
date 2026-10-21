#!/usr/bin/env python3
"""Download 386 PokeAPI Gen VII (40x30) pixel box icons and embed as ARGB1555.
Run in GitHub Actions with Pillow installed; no device-side download required.
"""
from concurrent.futures import ThreadPoolExecutor, as_completed
from io import BytesIO
from pathlib import Path
from urllib.request import Request, urlopen
from PIL import Image
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'src' / 'pokemon_icon_data.h'
BASE='https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/versions/generation-vii/icons'
W,H=40,30

def get_icon(dex):
    url=f'{BASE}/{dex}.png'
    for attempt in range(4):
        try:
            with urlopen(Request(url,headers={'User-Agent':'PokemonEditor-muOS/1.0'}),timeout=25) as response:
                data=response.read()
            image=Image.open(BytesIO(data)).convert('RGBA')
            if image.size != (W,H):
                raise ValueError(f'Unexpected dimensions for {dex}: {image.size}')
            pixels=[]
            for red,green,blue,alpha in image.getdata():
                # Opaque bit + RGB 5:5:5, 0 reserved for transparent.
                pixels.append(0 if alpha<96 else 0x8000 | ((red>>3)<<10) | ((green>>3)<<5) | (blue>>3))
            return dex,pixels
        except Exception:
            if attempt==3: raise
            time.sleep(1.5*(attempt+1))

if __name__ == '__main__':
    icons=[None]*386
    with ThreadPoolExecutor(max_workers=12) as pool:
        tasks={pool.submit(get_icon,dex):dex for dex in range(1,387)}
        for job in as_completed(tasks):
            dex,pixels=job.result()
            icons[dex-1]=pixels
    assert all(len(i)==W*H for i in icons)
    with OUT.open('w',encoding='ascii') as f:
        f.write('/* AUTO GENERATED from PokeAPI/sprites, pixel box icons (Gen VII). */\n')
        f.write('#ifndef POKEMON_ICON_DATA_H\n#define POKEMON_ICON_DATA_H\n')
        f.write('#define MINI_ICON_WIDTH 40\n#define MINI_ICON_HEIGHT 30\n')
        f.write('static const unsigned short mini_icon_data[386][1200]={\n')
        for icon in icons:
            f.write('{'+','.join(str(v) for v in icon)+'},\n')
        f.write('};\n#endif\n')
    print(f'Wrote {OUT} with 386 authentic species-specific pixel icons, {OUT.stat().st_size:,} bytes')
