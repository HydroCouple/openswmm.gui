# -*- coding: utf-8 -*-
"""Render this article into ../../articles.html, preserving existing blocks.
Run from this directory: python3 build.py [optional absolute website root].
GIF controls provide static posters and honor reduced-motion preferences.
The LinkedIn draft and preview are generated from the same article source.
The renderer uses only Python's standard library.
Adapted from the site's exact-cross-section-geometry article builder.
"""
import io, os, re, html, sys

HERE  = os.path.dirname(os.path.abspath(__file__))
SLUG  = 'lid-storage-node'
ROOT  = os.path.abspath(sys.argv[1]) if len(sys.argv)>1 else os.path.abspath(os.path.join(HERE, '..', '..'))
AUTHORS = ['Caleb Buahin']
DATE = '10-04-2026'
SRCLINK = ('articles/lid-storage-node/models/lid_active_chain.zip', 'Download the six test models →')

def url_of(u):
    if u.startswith(('http:', 'https:', '#')): return u
    if u.startswith('assets/'): return 'img/articles/' + SLUG + '/' + u[7:]
    if u.startswith('../../img/'): return u[6:]
    return 'articles/' + SLUG + '/' + u

MATH = []


def fmt(t):
    t = html.escape(t)
    t = t.replace('ₘ', '<sub>m</sub>').replace('ₛ', '<sub>s</sub>')  # portable mobile-store subscript
    t = re.sub(r'`([^`]+)`', lambda m: '<code>' + m.group(1) + '</code>', t)
    t = re.sub(r'\*\*([^*]+)\*\*', r'<strong>\1</strong>', t)
    t = re.sub(r'(?<!\*)\*([^*]+)\*(?!\*)', r'<em>\1</em>', t)
    return t


def inline(t):
    links = []
    def grab(m):
        links.append((m.group(1), url_of(m.group(2))))
        return '\x00%d\x00' % (len(links) - 1)
    t = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', grab, t)
    t = fmt(t)
    for i, (txt, u) in enumerate(links):
        t = t.replace('\x00%d\x00' % i,
                      '<a href="%s">%s</a>' % (html.escape(u, quote=True), fmt(txt)))
    return t


NUM_CELL = re.compile(r'^[~−\-+]?\d')  # leading digit, optionally signed/approx


def table(rows):
    """A column is right-aligned (class="num") only when EVERY data cell in it
    starts with a digit (allowing a leading ~/-/+/−) — so a prose table (all
    text) stays fully left-aligned, a numeric table right-aligns its data columns
    against a left-aligned label column, and a column mixing numbers with a text
    verdict (e.g. "no meaningful change") also stays left, which is the reading
    that column actually wants."""
    hdr = [c.strip() for c in rows[0].strip('|').split('|')]
    body = [[c.strip() for c in r.strip('|').split('|')] for r in rows[2:]]
    ncol = len(hdr)
    numeric = [bool(body) and all(NUM_CELL.match(r[i]) for r in body if i < len(r) and r[i])
              for i in range(ncol)]
    def cls(i):
        return ' class="num"' if numeric[i] else ''
    o = ['<table><thead><tr>' +
         ''.join('<th%s>%s</th>' % (cls(i), inline(c)) for i, c in enumerate(hdr)) +
         '</tr></thead><tbody>']
    for r in body:
        o.append('<tr>' + ''.join('<td%s>%s</td>' % (cls(i), inline(c)) for i, c in enumerate(r)) + '</tr>')
    return ''.join(o) + '</tbody></table>'


def render_body(body):
    out, L, i = [], body.split('\n'), 0
    while i < len(L):
        st = L[i].strip()
        if not st or st == '---' or st.startswith('<!--'):
            i += 1; continue
        if st.startswith('```'):
            code = []; i += 1
            while i < len(L) and not L[i].strip().startswith('```'):
                code.append(L[i]); i += 1
            if i == len(L): raise ValueError('Unclosed code fence')
            i += 1
            out.append('<pre><code>' + html.escape('\n'.join(code)) + '</code></pre>'); continue
        if st.startswith('## '):
            out.append('<h3>%s</h3>' % inline(st[3:])); i += 1; continue
        if st.startswith('### '):
            out.append('<h4>%s</h4>' % inline(st[4:])); i += 1; continue
        if st.startswith('!['):
            m = re.match(r'!\[([^\]]*)\]\(([^)]+)\)', st)
            path = m.group(2); alt = html.escape(m.group(1), quote=True)
            if path.endswith('.gif'):
                poster = path[:-4] + '-static.png'
                out.append('<figure class="lid-animation"><picture><source media="(prefers-reduced-motion: reduce)" srcset="%s"><img src="%s" alt="%s" loading="lazy" data-animated="%s" data-static="%s"></picture><button type="button">Show static figure</button></figure>' % (url_of(poster),url_of(path),alt,url_of(path),url_of(poster)))
            else:
                out.append('<figure><img src="%s" alt="%s" loading="lazy"></figure>' % (url_of(path),alt))
            i += 1; continue
        if st.startswith('|'):
            rows = []
            while i < len(L) and L[i].strip().startswith('|'):
                rows.append(L[i].strip()); i += 1
            out.append('<div class="table-scroll">' + table(rows) + '</div>'); continue
        if st.startswith('> ') or st == '>':
            buf = []
            while i < len(L) and (L[i].strip().startswith('> ') or L[i].strip() == '>'):
                buf.append(L[i].strip()[2:] if L[i].strip() != '>' else ''); i += 1
            ps = [p.strip() for p in '\n'.join(buf).split('\n\n') if p.strip()]
            out.append('<blockquote>' +
                       ''.join('<p>%s</p>' % inline(' '.join(p.split('\n'))) for p in ps) +
                       '</blockquote>')
            continue
        if st.startswith('- '):
            items = []
            while i < len(L) and (L[i].strip().startswith('- ') or
                                  (L[i].startswith('  ') and L[i].strip() and items)):
                if L[i].strip().startswith('- '): items.append(L[i].strip()[2:])
                else:                             items[-1] += ' ' + L[i].strip()
                i += 1
            out.append('<ul>' + ''.join('<li>%s</li>' % inline(x) for x in items) + '</ul>')
            continue
        buf = []
        while i < len(L) and L[i].strip() and \
                not L[i].strip().startswith(('#', '|', '>', '- ', '!', '---', '```', '<!--')):
            buf.append(L[i].strip()); i += 1
        out.append('<p>%s</p>' % inline(' '.join(buf)))

    return out

ANIMATION_SUPPORT = """<style>
.lid-animation button{margin:.5rem 0;padding:.45rem .8rem;border:1px solid #557080;border-radius:4px;background:transparent;color:inherit;cursor:pointer;font:inherit}
#lid-storage-node .article-body pre{overflow:auto;padding:1rem;background:#f1f5f7;color:#123047;border-radius:6px}
@media print{.lid-animation button{display:none}}
</style>
<script>
(function(){document.querySelectorAll('#lid-storage-node .lid-animation').forEach(function(fig){
const img=fig.querySelector('img'), button=fig.querySelector('button'), source=fig.querySelector('source');
let playing=!window.matchMedia('(prefers-reduced-motion: reduce)').matches;
if(!playing){img.src=img.dataset.static;button.textContent='Play animation';}
button.addEventListener('click',function(){if(source)source.remove();playing=!playing;img.src=playing?img.dataset.animated:img.dataset.static;button.textContent=playing?'Show static figure':'Play animation';});
});})();
</script>"""

def render(md):
    lines=md.split('\n')
    title, subtitle=lines[0][2:].strip(),lines[2][4:].strip()
    out=render_body('\n'.join(lines[6:]))
    meta = ''.join('              <span>%s</span>\n              <span>·</span>\n' % a
                   for a in AUTHORS)
    block = ('        <!-- ─────────── ARTICLE ─────────── -->\n'
             '        <article id="%s">\n'
             '          <div class="article-head">\n'
             '            <h2>%s</h2>\n'
             '            <div class="article-meta">\n%s'
             '              <span>%s</span>\n'
             '              <a class="src-link" href="%s">%s</a>\n'
             '            </div>\n'
             '          </div>\n'
             '          <div class="article-body">\n'
             '            <p><em>%s</em></p>\n%s\n'
             '          </div>\n'
             '        </article>\n\n'
             ) % (SLUG, html.escape(title), meta, DATE, SRCLINK[0], SRCLINK[1],
                  html.escape(subtitle),
                  '\n'.join('            ' + l for l in out) + '\n' + ANIMATION_SUPPORT)
    return title, block


def _mdY_to_sortkey(mdY):
    m, d, y = mdY.split('-')
    return (y, m, d)


def linkedin_page(title, block):
    # Reuse the exact website article; only asset/link paths and page chrome differ.
    block = block.replace('"img/articles/', '"../../img/articles/')
    block = block.replace('href="articles/' + SLUG + '/', 'href="')
    return ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width, initial-scale=1">'
            '<title>' + html.escape(title) + '</title>'
            '<style>body{font:18px/1.65 system-ui,sans-serif;color:#123047;'
            'background:#f7fafb;max-width:1000px;margin:3rem auto;padding:0 1.4rem}'
            'img{max-width:100%;height:auto}figure{margin:2rem 0}'
            'h2,h3,h4{line-height:1.2}h2{font-size:32px}'
            '.article-meta{font-size:16px}.article-meta span,.src-link{margin-right:.4rem}'
            'table{border-collapse:collapse;width:100%;font-size:16px}'
            'th,td{padding:.5rem;border-bottom:1px solid #dce6eb;text-align:left}'
            '.num{text-align:right}.table-scroll{overflow:auto}'
            'pre{padding:1rem;background:#eaf1f4;overflow:auto}a{color:#007a80}'
            'blockquote{border-left:4px solid #008f83;padding-left:1rem}'
            '</style></head><body><nav><a href="../../articles.html#' + SLUG + '">'
            '← Website article</a></nav>' + block + '</body></html>')


def main():
    article_dir = os.path.join(ROOT, 'articles', SLUG)
    md = io.open(os.path.join(article_dir, 'article.md'), encoding='utf-8').read()
    title, block = render(md)
    p = os.path.join(ROOT, 'articles.html')
    s = io.open(p, encoding='utf-8').read()

    # --- article block: replace in place, or insert as the newest ---
    pat = re.compile(r'([ \t]*<!-- [^\n]*ARTICLE[^\n]*-->\n)?[ \t]*<article id="%s">.*?</article>\n\n?'
                     % re.escape(SLUG), re.S)
    if pat.search(s):
        s = pat.sub(block, s, count=1)
    else:
        m = re.search(r'[ \t]*<!-- [^\n]*ARTICLE[^\n]*-->\n[ \t]*<article id="', s)
        if not m:
            sys.exit('could not find the first <article> block in articles.html')
        s = s[:m.start()] + block + s[m.start():]

    # --- index entry ---
    # The index is <ol id="articleIndex"> with <li><a href="#slug" data-target="slug">
    # <span class="art-date">MM-DD-YYYY</span><span class="art-title">Title</span></a></li>
    # entries, newest first (no counter/--art-start any more — that scheme was replaced
    # 2026-08-31; see build.py's DATE comment). Insert/move this article into the correct
    # chronological slot rather than always prepending, since a future article added here
    # is not guaranteed to be the newest one on the page.
    li = ('          <li><a href="#%s" data-target="%s">'
          '<span class="art-date">%s</span><span class="art-title">%s</span></a></li>\n'
          % (SLUG, SLUG, DATE, html.escape(title)))

    s = re.sub(r'[ \t]*<li><a href="#%s" data-target="%s">.*?</a></li>\n' % (SLUG, SLUG), '', s)

    ol = re.search(r'<ol id="articleIndex"[^>]*>\n', s)
    if not ol:
        sys.exit('could not find <ol id="articleIndex"> in articles.html')
    body_start = ol.end()
    body_end = body_start + s[body_start:].index('</ol>')
    region = s[body_start:body_end]
    # Every <li> ends in its own trailing "\n"; only the pure-whitespace indent
    # of the closing tag itself (e.g. "        ") is left over after the last
    # one, and must be re-attached rather than dropped, or the reconstructed
    # </ol> loses its indentation. (An earlier version instead let an optional
    # leading \n in the close-tag pattern greedily eat the LAST <li>'s own
    # terminating newline, silently dropping that item from findall — caught
    # by diffing against the six pre-existing articles before pushing.)
    tail_ws = re.search(r'[ \t]*\Z', region).group(0)
    items = re.findall(r'[ \t]*<li>.*?</li>\n', region[:len(region) - len(tail_ws)], re.S)
    dates = [re.search(r'class="art-date">([^<]+)<', it) for it in items]
    key = _mdY_to_sortkey(DATE)
    idx = next((i for i, d in enumerate(dates) if d and _mdY_to_sortkey(d.group(1)) < key), len(items))
    items.insert(idx, li)
    s = s[:body_start] + ''.join(items) + tail_ws + s[body_end:]

    io.open(p, 'w', encoding='utf-8').write(s)
    io.open(os.path.join(article_dir, 'linkedin-article.md'), 'w', encoding='utf-8').write(md)
    io.open(os.path.join(article_dir, 'linkedin-article.html'), 'w', encoding='utf-8').write(linkedin_page(title, block))
    print('articles.html updated: %s' % SLUG)


if __name__ == '__main__':
    main()
