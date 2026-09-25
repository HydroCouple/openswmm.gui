#!/usr/bin/env python3
"""Regenerate the W0 source inventory; this is discovery, not UI acceptance.

Run from any directory. Outputs are reviewable, deterministic Markdown/JSON in
 docs/gui-baseline. Keep human findings in README.md; generated files may be
replaced. No build, user settings, model, or runtime files are changed.
"""
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs/gui-baseline"
OUT.mkdir(parents=True, exist_ok=True)


def without_comments(text):
    # Preserve strings and line positions so source references stay useful.
    pattern = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/'
    return re.sub(pattern, lambda m: re.sub(r'[^\n]', ' ', m[0])
                  if m[0].startswith(('//', '/*')) else m[0], text)


files = sorted(p for base in ('include', 'src') for p in (ROOT / base).rglob('*')
               if p.suffix in ('.h', '.cpp'))
code = {p.relative_to(ROOT).as_posix(): without_comments(p.read_text()) for p in files}
tests = {p.relative_to(ROOT).as_posix(): without_comments(p.read_text())
         for p in sorted((ROOT / 'tests/gui').glob('test_*.cpp'))}


def locations(texts, pattern):
    return [f'{path}:{text.count(chr(10), 0, m.start()) + 1}'
            for path, text in texts.items() for m in re.finditer(pattern, text)]


def dump(name, data):
    (OUT / name).write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n')


classes = []
for path, text in code.items():
    for m in re.finditer(r'\bclass\s+(\w+)\s*(?:final\s*)?:\s*public\s+([\w:]+)[^{;]*\{', text):
        classes.append((m[1], m[2], path, text.count('\n', 0, m.start()) + 1))

widgets = {'QDialog', 'QWidget', 'QScrollArea', 'QFrame', 'QTabWidget', 'QWizard', 'QBasePropertyItemEditor'}
dialogs = {'QDialog', 'QWizard'}
for _ in range(len(classes)):
    before = len(widgets)
    for name, base, _, _ in classes:
        if base in widgets:
            widgets.add(name)
        if base in dialogs:
            dialogs.add(name)
    if len(widgets) == before:
        break

manifest = []
for name, base, path, line in classes:
    is_dialog = name in dialogs
    if not is_dialog and not ('ui/dialogs/' in path and name in widgets):
        continue
    pattern = rf'\b{re.escape(name)}\b'
    refs = locations({p: t for p, t in code.items()
                      if Path(p).stem != Path(path).stem and p.startswith('src/')}, pattern)
    implementations = [p for p in code if p.startswith('src/') and
                       (Path(p).stem == Path(path).stem or re.search(rf'\b{name}::{name}\s*\(', code[p]))]
    shared = sorted({control for p in implementations
                     for control in re.findall(r'\b(ColorButton|QDialogButtonBox|LabelConfigEditor|ClassificationEditor|DialogLayoutPersistence|RelativePathPicker|SeriesStyleEditor)\b', code[p])})
    manifest.append({'id': f'{path}::{name}', 'class': name, 'base': base,
                     'kind': 'dialog' if is_dialog else 'embedded surface',
                     'declaration': f'{path}:{line}', 'implementation': implementations,
                     'entryPointCandidates': refs, 'sharedComponents': shared,
                     'testReferences': locations(tests, pattern),
                     'auditStatus': 'pending workflow/keyboard/theme/lifetime verification',
                     'ownerAndPersistence': 'resolve from constructor and launch path in W3',
                     'requiredChanges': 'apply family contract after workflow audit; see README',
                     'verification': 'source discovery only; a test reference is not a passing test'})
manifest.sort(key=lambda row: row['id'])
dump('dialogs.json', manifest)

# Keep anonymous/platform prompts separate; they do not have dialog classes.
prompts = locations(code, r'\b(?:QMessageBox|QFileDialog|QInputDialog|QColorDialog|QFontDialog)\s*(?:::|\*?\s+\w+\s*[({])')
dump('standard-dialog-sites.json', prompts)

# Every implementation file gets a disposition, including non-widget helpers;
# declarations and inheritance alone cannot discover dynamically assembled pages.
implementation_files = []
for path in code:
    if path.startswith('src/ui/dialogs/'):
        names = [row['class'] for row in manifest if path in row['implementation']]
        implementation_files.append({'path': path, 'manifestClasses': names,
                                     'status': 'mapped' if names else 'helper or dynamic surface: inspect in W3'})
dump('dialog-files.json', implementation_files)

# Actual catalog aliases take precedence over constructor/Designer icons when
# the main-window theme sweep runs. Raw icon calls are candidates, not bypasses.
aliases = {}
for qrc in sorted((ROOT / 'resources').glob('*.qrc')):
    for resource in ET.parse(qrc).getroot().findall('qresource'):
        for item in resource.findall('file'):
            alias = item.get('alias', item.text)
            aliases[resource.get('prefix', '').rstrip('/') + '/' + alias] = {
                'source': (qrc.parent / item.text).relative_to(ROOT).as_posix(),
                'exists': (qrc.parent / item.text).exists()}
actions = []
for m in re.finditer(r'\{\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,([^}]+)\}', code['include/ui/actioncatalog.h'].split('kActionCatalog[] = {', 1)[1].split('\n};', 1)[0]):
    actions.append(dict(zip(('id', 'objectName', 'category', 'shortcut', 'icon', 'tab', 'menu', 'tags'), m.groups())))
for row in actions:
    row['asset'] = aliases.get('/swmmvis/' + row['icon']) if row['icon'] else None
    row['review'] = 'pending illustrative/native-size/theme review'
dump('action-icons.json', actions)
dump('icon-call-sites.json', locations(code, r'\b(?:QIcon\s*\(|IconFactory::icon\s*\(|setIcon\s*\()'))
dump('theme-paint-sites.json', locations(code, r'\b(?:setBrush|fillRect|setPen|setStyleSheet)\s*\('))
dump('registry-sites.json', locations(code, r'\b(?:ComprehensiveEditorRegistry|PropertyEditorRegistry|StyleEditorRegistry|DialogRegistry|ImportTargetRegistry)\b'))

lines = ['# Generated W0 discovery index', '',
         'Regenerate with `python3 scripts/audit_gui_baseline.py`. Read [the audit](README.md) for scope, evidence and acceptance gates.', '',
         '**Source inventory only.** Reference sites include launchers and other consumers; comments are excluded. Runtime registration, reachability, owner/persistence, anonymous pages and plugin-created surfaces require the separate audit. No row is marked visually or functionally accepted by this script.', '',
         f'- {sum(r["kind"] == "dialog" for r in manifest)} dialog declarations; {sum(r["kind"] != "dialog" for r in manifest)} embedded surface declarations.',
         f'- {len(implementation_files)} dialog implementation files; {sum(not r["manifestClasses"] for r in implementation_files)} need helper/dynamic-surface classification.',
         f'- {len(prompts)} standard-dialog reference sites.',
         f'- {len(actions)} catalog actions; {sum(bool(r["icon"]) for r in actions)} with icon aliases.',
         '', '| Surface | Kind | Declaration | Entry/consumer sites | Test references |',
         '|---|---|---|---|---|']
for row in manifest:
    lines.append(f'| {row["class"]} | {row["kind"]} | `{row["declaration"]}` | {len(row["entryPointCandidates"])} | {len(row["testReferences"])} |')
(OUT / 'INDEX.md').write_text('\n'.join(lines) + '\n')
print('\n'.join(lines[6:10]))
