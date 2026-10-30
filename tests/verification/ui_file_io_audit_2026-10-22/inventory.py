"""Source inventory supporting the audit; discovery is not a passing test."""
from pathlib import Path
import re, json, hashlib, subprocess

root=Path(__file__).resolve().parent
gui=root.parents[2]
engine=gui.parent/'openswmm.engine'
properties=[]
for f in sorted((gui/'include/ui/properties').glob('swmm*propertyadapter.h')):
    s=f.read_text()
    for m in re.finditer(r'Q_PROPERTY\((.*?)\)',s,re.S):
        declaration=' '.join(m.group(1).split())
        if ' WRITE ' in declaration:
            properties.append({'file':str(f),'line':s[:m.start()].count('\n')+1,'declaration':declaration})
options=[]
for f in sorted((gui/'src/ui/dialogs/simoptions').glob('*.cpp')):
    s=f.read_text()
    for m in re.finditer(r'tagOption\([^;]*?"([A-Z][A-Z0-9_\[\]]*)"\s*\)',s):
        options.append({'file':str(f),'line':s[:m.start()].count('\n')+1,'key':m.group(1)})
f=engine/'src/engine/core/InpWriter.cpp'
precision=[]
section='helper'
for line_no,line in enumerate(f.read_text().splitlines(),1):
    match=re.search(r'sec\(f, "([^"]+)"\)',line)
    if match: section=match.group(1)
    formats=re.findall(r'%(?:[-+0-9]*\.)?(?:\d+)?[fg]',line)
    formats=[v for v in formats if not re.search(r'\.1[567][fg]$',v)]
    if formats and '"' in line and not line.lstrip().startswith('//'):
        precision.append({'file':str(f),'line':line_no,'nearest_section':section,'formats':formats,'source':line.strip()})
sources=[f,engine/'src/engine/input/geopackage/GeoPackageWriter.cpp',engine/'src/engine/input/geopackage/GeoPackageReader.cpp',gui/'src/project/projectserializer.cpp']
sources += sorted((gui/'src/ui/dialogs/simoptions').glob('*.cpp'))
out={'note':'Inventory of direct writable SWMM properties and literal option tags, not a complete UI registry or proof of round-trip coverage. Dynamic keys and compound editors require separate review.',
     'revisions_at_inventory':{p.name:subprocess.check_output(['git','-C',str(p),'rev-parse','HEAD'],text=True).strip() for p in [gui,engine]},
     'writable_properties':properties,'literal_tagged_options':options,'limited_precision_formats':precision,
     'source_sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
(root/'source_inventory.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps({'writable_property_declarations':len(properties),'property_headers':len(set(x['file'] for x in properties)),'literal_option_tags':len(options),'limited_precision_format_lines':len(precision),'revisions':out['revisions_at_inventory']},indent=2))
