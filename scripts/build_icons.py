#!/usr/bin/env python3
"""Download 386 PokeAPI Gen VII (40x30) pixel box icons and embed as ARGB1555.
Run in GitHub Actions with Pillow installed; no device-side download required.
"""
from concurrent.futures import ThreadPoolExecutor, as_completed
from io import BytesIO
from pathlib import Path
from urllib.request import Request, urlopen
from PIL import Image, ImageDraw
import re, csv, hashlib
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
            return dex,pixels,data
        except Exception:
            if attempt==3: raise
            time.sleep(1.5*(attempt+1))

if __name__ == '__main__':
    icons=[None]*386
    originals=[None]*386
    hashes=[None]*386
    with ThreadPoolExecutor(max_workers=12) as pool:
        tasks={pool.submit(get_icon,dex):dex for dex in range(1,387)}
        for job in as_completed(tasks):
            dex,pixels,data=job.result()
            icons[dex-1]=pixels
            originals[dex-1]=Image.open(BytesIO(data)).convert("RGBA")
            hashes[dex-1]=hashlib.sha256(data).hexdigest()
    assert all(len(i)==W*H for i in icons)
    assert all(im is not None and im.getbbox() is not None for im in originals), "Missing/blank icon"
    name_header=(ROOT/'src/pokemon_names_data.h').read_text()
    names=re.findall(r'^"([^"\n]+)",$', name_header, re.MULTILINE)
    assert len(names)==387, f"Expected 387 names including index zero, got {len(names)}"
    with (ROOT/'sprite-index.csv').open('w',newline='') as fp:
        writer=csv.writer(fp);writer.writerow(['national_dex','pokemon_name','png_sha256','sprite_source'])
        for dex in range(1,387):
            writer.writerow([dex,names[dex],hashes[dex-1],f'{BASE}/{dex}.png'])
    # A review sheet of *every* species is part of the GitHub artifact.
    # This deliberately uses National Dex numbers, not Gen 3 internal IDs.
    cols=12;rows=(386+cols-1)//cols
    sheet=Image.new("RGB",(cols*92,rows*56),(20,28,43))
    draw=ImageDraw.Draw(sheet)
    for dex,im in enumerate(originals,1):
        x=((dex-1)%cols)*92;y=((dex-1)//cols)*56
        sheet.paste(im,(x+25,y+2),im)
        draw.text((x+5,y+37),f"{dex:03d} {names[dex][:11]}",fill=(230,240,248))
    review=ROOT/'sprite-review-all-386.png'
    sheet.save(review)
    print(f"Sprite review sheet: {review}")
    with OUT.open('w',encoding='ascii') as f:
        f.write('/* AUTO GENERATED from PokeAPI/sprites, pixel box icons (Gen VII). */\n')
        f.write('#ifndef POKEMON_ICON_DATA_H\n#define POKEMON_ICON_DATA_H\n')
        f.write('#define MINI_ICON_WIDTH 40\n#define MINI_ICON_HEIGHT 30\n')
        f.write('static const unsigned short mini_icon_data[386][1200]={\n')
        for icon in icons:
            f.write('{'+','.join(str(v) for v in icon)+'},\n')
        f.write('};\n#endif\n')
    print(f'Wrote {OUT} with 386 authentic species-specific pixel icons, {OUT.stat().st_size:,} bytes')
