#!/usr/bin/env python3
"""Bundle National Dex species names (English canonical identifiers) offline."""
from pathlib import Path
from urllib.request import urlopen,Request
import csv,io
root=Path(__file__).resolve().parents[1]
url='https://raw.githubusercontent.com/veekun/pokedex/master/pokedex/data/csv/pokemon_species.csv'
with urlopen(Request(url,headers={'User-Agent':'MuOS-Pokemon-Editor/1.1'}),timeout=35) as f:
    rows=list(csv.DictReader(io.StringIO(f.read().decode('utf-8'))))
names=['UNKNOWN']*387
for row in rows:
    n=int(row['id'])
    if 1<=n<=386:
        names[n]=row['identifier'].upper().replace('-', ' ')
assert all(name!='UNKNOWN' for name in names[1:])
out=root/'src/pokemon_names_data.h'
with out.open('w') as f:
    f.write('/* Generated from veekun species data. */\n')
    f.write('static const char *pokemon_names[387]={\n')
    for n in names:
        f.write('"'+n.replace('"','')+'",\n')
    f.write('};\n')
print('Generated',out)
