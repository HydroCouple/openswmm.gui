#!/usr/bin/env python3
"""Compose native ribbon captures at a common scale, preserving the UI pixels."""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path, help='Directory containing ribbon-full/compact/collapsed.png')
parser.add_argument('--font', type=Path, default=Path('/System/Library/Fonts/Helvetica.ttc'))
args = parser.parse_args()
canvas = Image.new('RGB', (1400, 410), 'white')
draw = ImageDraw.Draw(canvas)
font = ImageFont.truetype(str(args.font), 18)
y = 8
for stem, width, label in [('full',1400,'1600 px window: labels visible'), ('compact',963,'1100 px window: trailing groups use compact buttons'), ('collapsed',700,'800 px window: Import and Run groups collapse into popup buttons')]:
    draw.text((8,y),label,fill='#263238',font=font)
    y += 28
    im=Image.open(args.directory/f'ribbon-{stem}.png').convert('RGB')
    im=im.resize((width,round(im.height*width/im.width)),Image.Resampling.LANCZOS)
    canvas.paste(im,(0,y))
    y += im.height+18
canvas.crop((0,0,1400,y)).save(args.directory/'02_ribbon_compact_modes.png',optimize=True)
