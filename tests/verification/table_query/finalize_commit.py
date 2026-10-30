"""Commit the reviewed snapshots and preserve unrelated staging and work files."""
from pathlib import Path
import hashlib
import json
import os
import subprocess

root = Path(__file__).resolve().parents[3]
review = Path(__file__).resolve().parent / 'commit_review'
manifest = json.loads((review / 'manifest.json').read_text())
env = dict(os.environ, GIT_INDEX_FILE=str(review / 'feature.index'))

def git(*args, alternate=False):
    return subprocess.check_output(['git', *args], cwd=root, env=env if alternate else None)

assert git('rev-parse', 'HEAD').decode().strip() == manifest['base_head']
assert (root / '.git/index').read_bytes() == (review / 'original.index').read_bytes()
assert git('diff', '--cached', '--binary', alternate=True) == (review / 'feature.patch').read_bytes()
git('diff', '--cached', '--check', alternate=True)
assert not git('diff', '--cached', '--name-only', '--', *manifest['paths'])
worktree_paths = set(git('diff', '--name-only').decode().splitlines()) | set(manifest['paths'])
worktree_hashes = {p:hashlib.sha256((root / p).read_bytes()).hexdigest()
                   for p in worktree_paths if (root / p).is_file()}

message = review / 'commit-message.txt'
message.write_text('''feat(gui): resize dialog tables and expand attribute queries

Make dialog table columns adjustable and add numeric sorting to result and
picker tables while retaining stable row identities and ordered input editors.

Extend attribute predicates with NOT IN/LIKE/BETWEEN, NULL checks, strict
validation and context-aware completion. Apply queries consistently across
SWMM, mesh, data-object and GIS sources; preserve map selection for CSV/TSV.

Document table controls, suggestion shortcuts, query examples, selection
modes and source coverage in the GUI manual and feature specification.

Validation in the shared working checkout: application build, 27 parser tests,
11 GUI suites and manual link audit passed. Retain verification evidence.
''')
print(git('commit', '--file', str(message), alternate=True).decode())
commit = git('rev-parse', 'HEAD').decode().strip()
assert git('rev-parse', 'HEAD^').decode().strip() == manifest['base_head']
# Own paths had no preexisting staging. Bring only their shared index entries
# to the new commit; unrelated index entries and all working files stay intact.
git('reset', 'HEAD', '--', *manifest['paths'])
assert git('diff', '--cached', '--binary') == (review / 'original-staged.patch').read_bytes()
assert worktree_hashes == {p:hashlib.sha256((root / p).read_bytes()).hexdigest() for p in worktree_hashes}
result = {'commit':commit, 'unrelated_staging_preserved':True,
          'working_files_preserved':True, 'committed_paths':len(manifest['paths'])}
(review / 'commit-result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
