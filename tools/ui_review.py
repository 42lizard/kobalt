#!/usr/bin/env python3
"""Retain screenshot differences and a local chronological refactor journal."""
import argparse
from datetime import datetime, timezone
import hashlib
import html
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import urllib.parse

from native_screenshots import GROUPS

ROOT = Path(__file__).resolve().parents[1]
ENVIRONMENT = ('width', 'height', 'qt', 'platform', 'ppi', 'reading_ppi', 'dpr',
               'style', 'font_profile', 'device_fixture', 'assets_sha256', 'clock', 'battery_percent')


def read_json(path, default):
    return json.loads(path.read_text()) if path.exists() else default


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')
    temporary.replace(path)


def stamp():
    return datetime.now(timezone.utc).isoformat()


def asset(path, folder):
    content = path.read_bytes()
    name = hashlib.sha256(content).hexdigest() + path.suffix.lower()
    destination = folder / 'assets' / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists():
        destination.write_bytes(content)
    return 'assets/' + name


def page(title, body):
    css = """
:root{color-scheme:light;--ink:#223049;--muted:#647087;--line:#dde3ec;--accent:#315fa1}
*{box-sizing:border-box}body{margin:0;background:#f4f6fa;color:var(--ink);font:15px/1.65 -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}
a{color:var(--accent);text-underline-offset:3px}nav{display:flex;gap:24px;flex-wrap:wrap;padding:18px 0;border-bottom:1px solid var(--line)}
main{max-width:1100px;margin:auto;padding:0 26px 56px}header{padding:30px 0 18px}h1{font-size:32px;letter-spacing:-.8px;margin:0}h2{font-size:20px;line-height:1.4;margin:0 0 12px}h3{font-size:16px;margin:0 0 6px}p{margin:8px 0;white-space:pre-line}.muted,time{color:var(--muted);font-size:13px}
article,.progress,.run{background:white;border:1px solid var(--line);border-radius:12px;padding:22px;margin:14px 0;overflow-wrap:anywhere}.progress{border-left:4px solid var(--accent);background:#fafdff}.eyebrow{font-size:12px;text-transform:uppercase;letter-spacing:1px;color:var(--muted);margin-bottom:8px}
.tests{padding:0;list-style:none}.tests li{display:flex;align-items:baseline;gap:10px;margin:8px 0}.badge{font-size:11px;line-height:1.6;font-weight:600;letter-spacing:.3px;border-radius:5px;padding:2px 8px;white-space:nowrap;background:#eef1f6;color:#536078}.passed{background:#e7f3eb;color:#276a40}.failed{background:#fbe9e9;color:#a12f37}.blocked{background:#fff0d7;color:#92600d}
.stats{display:flex;flex-wrap:wrap;gap:24px;margin:12px 0}.stats strong{font-size:24px;margin-right:6px}.pair,.evidence{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px}figure{margin:12px 0}figcaption{font-size:13px;color:var(--muted);margin-bottom:8px}img{display:block;max-width:100%;max-height:360px;width:auto;height:auto;margin:auto;border:1px solid var(--line);border-radius:6px;background:#fafafa}figure a{display:block;background:#f5f7fa;border-radius:8px;padding:12px}
details{border:1px solid var(--line);border-radius:8px;padding:12px 16px;margin:12px 0;background:#fff}details details{background:#f8f9fb}summary{cursor:pointer;font-weight:600}pre{white-space:pre-wrap;font-size:12px;line-height:1.6;overflow-wrap:anywhere}ul.files{font:13px/1.8 monospace;padding-left:20px}.archive{margin-top:28px}.empty{color:var(--muted);padding:18px 0}
@media(max-width:600px){main{padding:0 16px 32px}h1{font-size:27px}article,.progress,.run{padding:16px}.pair,.evidence{grid-template-columns:1fr}nav{gap:16px}img{max-height:300px}}
"""
    script = """
const key='ui-review-view:'+location.pathname;let restored=false;
try{const saved=JSON.parse(sessionStorage.getItem(key)||'null');if(saved){for(const [id,open] of Object.entries(saved.details||{})){const el=document.getElementById(id);if(el)el.open=open;}if(saved.hash===location.hash){restored=true;requestAnimationFrame(()=>scrollTo(0,saved.scroll));}}}catch(e){}
addEventListener('beforeunload',()=>{try{sessionStorage.setItem(key,JSON.stringify({scroll:scrollY,hash:location.hash,details:Object.fromEntries([...document.querySelectorAll('details[id]')].map(el=>[el.id,el.open]))}));}catch(e){}});
if(location.hash&&!restored){const target=document.getElementById(location.hash.slice(1));if(target){for(let el=target.parentElement;el;el=el.parentElement)if(el.tagName==='DETAILS')el.open=true;target.scrollIntoView();}}
"""
    return ('<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">'
            '<meta http-equiv="refresh" content="20"><title>' + html.escape(title) + '</title><style>' + css
            + '</style></head><body><main><nav><a href="/refactor-journal/index.html">Journal</a>'
            '<a href="/screenshots/ui-review.html">Visual review</a><a href="/screenshots/index.html">Gallery</a></nav>'
            '<header><h1>' + html.escape(title) + '</h1><p class="muted">Local refactor evidence · Updates every 20 seconds</p></header>'
            + body + '</main><script>' + script + '</script></body></html>')


def time_label(timestamp):
    try:
        value = datetime.fromisoformat(timestamp.replace('Z', '+00:00'))
        label = value.strftime('%d %b %Y · %H:%M:%S') + (' UTC' if value.utcoffset().total_seconds() == 0 else value.strftime(' %z'))
    except (ValueError, AttributeError):
        label = timestamp
    return '<time datetime="' + html.escape(timestamp, quote=True) + '">' + html.escape(label) + '</time>'


def test_list(tests):
    items = []
    for test in tests:
        status, _, description = test.partition(':')
        badge = status if status in ('passed', 'failed', 'blocked') else 'unknown'
        items.append('<li><span class="badge ' + badge + '">' + html.escape(status.upper())
                     + '</span><span>' + html.escape(description) + '</span></li>')
    return '<ul class="tests">' + ''.join(items) + '</ul>' if items else ''


def latest_progress(build):
    entries = read_json(build / 'refactor-journal/entries.json', [])
    if not entries:
        return '<section class="progress"><div class="eyebrow">Latest progress</div><p>No progress entries yet.</p></section>'
    item = entries[-1]
    return ('<section class="progress"><div class="eyebrow">Latest progress</div><h2>' + html.escape(item['phase'])
            + '</h2>' + time_label(item['timestamp']) + '<p>' + html.escape(item['message'])
            + '</p>' + test_list(item['tests']) + '<a href="/refactor-journal/index.html#' + item['id'] + '">Read journal entry</a></section>')


def evidence_image(image, caption):
    return ('<figure><figcaption>' + html.escape(caption) + ' · Select to open full image</figcaption><a href="'
            + html.escape(image, quote=True) + '" target="_blank" rel="noopener"><img loading="lazy" src="'
            + html.escape(image, quote=True) + '" alt="' + html.escape(caption, quote=True) + '"></a></figure>')


def capture_set(folder):
    manifest = read_json(folder / 'manifest.json', {})
    if not manifest.get('complete'):
        raise ValueError('Comparison requires complete capture manifests')
    result = {}
    for shot in manifest['screens']:
        # A screen identity does not depend on a capture's output filename.
        key = (shot['group'], Path(shot['file']).stem, shot['theme'], str(shot.get('ppi', 300)))
        if key in result:
            raise ValueError('Duplicate capture identity: ' + str(key))
        path = (folder / shot['file']).resolve()
        if not path.is_relative_to(folder.resolve()) or not path.is_file():
            raise ValueError('Capture path is missing or outside its folder')
        environment = {name: shot.get(name, manifest.get(name)) for name in ENVIRONMENT}
        environment['ppi'] = shot.get('ppi', 300)
        environment['reading_ppi'] = shot.get('reading_ppi', 300 if environment['ppi'] == 300 else None)
        book = shot.get('book', GROUPS.get(shot['group'], ('alice' if shot['group'] == 'android-device' else None,))[0])
        environment['book'] = manifest.get('books', {}).get(book)
        if shot['group'] == 'dictionary':
            environment['dictionary'] = manifest.get('dictionaries', {}).get('en-en')
            environment['dictionary_reference'] = shot.get('dictionary_reference')
            environment['dictionary_fixture'] = shot.get('dictionary_fixture')
        result[key] = dict(metadata=shot, environment=environment, path=path,
                           sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    return result


def render_review(folder, records):
    folder.mkdir(parents=True, exist_ok=True)
    batches = {}
    for record in reversed(records):
        images = ''.join(evidence_image(record[label]['image'], label.capitalize())
                         for label in ('before', 'after') if record.get(label))
        details = {label: record[label]['environment'] for label in ('before', 'after') if record.get(label)}
        sources = {label: record[label]['source'] for label in ('before', 'after') if record.get(label)}
        article = ('<article id="' + record['id'] + '"><div class="eyebrow">' + html.escape(record['kind'])
                   + '</div><h2>' + html.escape('/'.join(record['screen'])) + '</h2>' + time_label(record['timestamp'])
                   + '<p><strong>' + html.escape(record['classification']) + '</strong></p><p>' + html.escape(record['cause'])
                   + '</p><div class="pair">' + images + '</div><details id="metadata-' + record['id']
                   + '"><summary>Environment and source metadata</summary><pre>'
                   + html.escape(json.dumps(dict(environment=details, source=sources), indent=2)) + '</pre></details></article>')
        historical = 'outside phase' in record['classification'].lower()
        batches.setdefault((record['batch'], historical), []).append(article)
    runs = read_json(folder / 'ui-review-runs.json', [])
    summaries = '<h2>Recent comparisons</h2>' + ''.join('<section class="run"><h3>' + html.escape(run['batch'])
        + '</h3>' + time_label(run['timestamp']) + '<div class="stats"><span><strong>'
        + str(run['unchanged']) + '</strong> unchanged</span><span><strong>' + str(run['differences'])
        + '</strong> ' + ('difference' if run['differences'] == 1 else 'differences') + '</span></div></section>' for run in reversed(runs))
    groups = ''
    for (batch, historical), articles in batches.items():
        identity = batch + str(historical)
        label = ('Historical capture inventory · ' if historical else '') + batch
        groups += ('<details class="archive" id="batch-' + hashlib.sha256(identity.encode()).hexdigest()[:12]
                   + '"' + ('' if historical else ' open') + '><summary>' + html.escape(label)
                   + f' · {len(articles)} ' + ('record' if len(articles) == 1 else 'records') + '</summary>' + ''.join(articles) + '</details>')
    (folder / 'ui-review.html').write_text(page('Visual review', latest_progress(folder.parent) + summaries
        + '<h2>Retained visual evidence</h2>' + (groups or '<p class="empty">No image differences recorded.</p>')))


def compare(before, after, build, batch, classification='unexpected regression', cause='Unknown',
            expected_environment_changes=None):
    left, right = capture_set(before), capture_set(after)
    folder = build / 'screenshots'
    folder.mkdir(parents=True, exist_ok=True)
    records = read_json(folder / 'ui-review.json', [])
    pending = []
    unchanged = 0
    for key in sorted(left.keys() | right.keys()):
        a, b = left.get(key), right.get(key)
        if a and b and a['environment'] != b['environment']:
            # Font updates deliberately change asset provenance. Require exact
            # before/after values; all other environment checks remain strict.
            expected = expected_environment_changes or {}
            for field, value in a['environment'].items():
                other = b['environment'][field]
                if value != other and expected.get(field) != (value, other):
                    raise ValueError('Capture environments differ for ' + '/'.join(key))
        if a and b and a['sha256'] == b['sha256']:
            unchanged += 1
            continue
        kind = 'changed' if a and b else 'missing' if a else 'additional'
        record = dict(id=f'difference-{len(records) + len(pending) + 1}', timestamp=stamp(), batch=batch,
                      screen=list(key), kind=kind, classification=classification, cause=cause)
        for label, item in [('before', a), ('after', b)]:
            if item:
                record[label] = dict(image=asset(item['path'], folder), sha256=item['sha256'],
                                     environment=item['environment'], source=item['metadata'].get('binary_sha256', 'Unknown'))
        pending.append(record)
    records.extend(pending)
    write_json(folder / 'ui-review.json', records)
    runs = read_json(folder / 'ui-review-runs.json', [])
    runs.append(dict(timestamp=stamp(), batch=batch, unchanged=unchanged, differences=len(pending)))
    write_json(folder / 'ui-review-runs.json', runs)
    render_review(folder, records)
    return dict(unchanged=unchanged, differences=len(pending), entries=[entry['id'] for entry in pending])


def journal(build, phase, message, files=(), tests=(), images=(), reviews=()):
    folder = build / 'refactor-journal'
    folder.mkdir(parents=True, exist_ok=True)
    entries = read_json(folder / 'entries.json', [])
    for test in tests:
        if test.split(':', 1)[0] not in ('passed', 'failed', 'blocked'):
            raise ValueError('Tests require passed:, failed: or blocked: status')
    entry = dict(id=f'entry-{len(entries) + 1}', timestamp=stamp(), phase=phase, message=message,
                 files=list(files), tests=list(tests), images=[asset(Path(image), folder) for image in images], reviews=list(reviews))
    entries.append(entry)
    write_json(folder / 'entries.json', entries)
    render_journal(folder)
    screenshots = build / 'screenshots'
    render_review(screenshots, read_json(screenshots / 'ui-review.json', []))
    return entry['id']


def journal_body(item):
    body = '<p>' + html.escape(item['message']) + '</p>' + test_list(item['tests'])
    if item['files']:
        body += '<details id="files-' + item['id'] + '"><summary>Affected files</summary><ul class="files">'
        body += ''.join('<li>' + html.escape(file) + '</li>' for file in item['files']) + '</ul></details>'
    body += '<div class="evidence">' + ''.join(evidence_image(image, 'Journal evidence') for image in item['images']) + '</div>'
    body += ''.join('<p><a href="/screenshots/ui-review.html#' + urllib.parse.quote(review) + '">Visual difference '
                    + html.escape(review) + '</a></p>' for review in item['reviews'])
    return body


def render_journal(folder):
    folder.mkdir(parents=True, exist_ok=True)
    entries = read_json(folder / 'entries.json', [])
    articles = []
    for item in reversed(entries[:-1]):
        articles.append('<article id="' + item['id'] + '"><h2>' + html.escape(item['phase'])
                        + '</h2>' + time_label(item['timestamp']) + journal_body(item) + '</article>')
    latest = '<p class="empty">No progress entries yet.</p>'
    jump = ''
    if entries:
        item = entries[-1]
        latest = ('<article class="progress" id="' + item['id'] + '"><div class="eyebrow">Latest progress</div><h2>'
                  + html.escape(item['phase']) + '</h2>' + time_label(item['timestamp']) + journal_body(item) + '</article>')
        jump = '<a href="#' + item['id'] + '">Jump to latest</a>'
    (folder / 'index.html').write_text(page('Refactor journal', latest + jump
        + '<h2>Earlier progress</h2><p class="muted">Newest entries first. Earlier evidence stays intact.</p>'
        + ''.join(articles)))


def server(build, port=0):
    build = build.resolve()

    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(build), **kwargs)

        def allowed_path(self):
            path = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path)
            target = (build / path.lstrip('/')).resolve()
            allowed = (build / 'screenshots', build / 'refactor-journal')
            if not any(target.is_relative_to(root) for root in allowed) or not target.is_file():
                self.send_error(404)
                return False
            return True

        def do_GET(self):
            if self.allowed_path():
                super().do_GET()

        def do_HEAD(self):
            if self.allowed_path():
                super().do_HEAD()

    try:
        return ThreadingHTTPServer(('127.0.0.1', port), Handler)
    except OSError:
        if not port:
            raise
        return ThreadingHTTPServer(('127.0.0.1', 0), Handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build')
    commands = parser.add_subparsers(dest='command', required=True)
    comparison = commands.add_parser('compare')
    comparison.add_argument('before', type=Path)
    comparison.add_argument('after', type=Path)
    comparison.add_argument('--batch', required=True)
    comparison.add_argument('--classification', default='unexpected regression')
    comparison.add_argument('--cause', default='Unknown')
    log = commands.add_parser('journal')
    log.add_argument('--phase', required=True)
    log.add_argument('--message', required=True)
    for option in ('file', 'test', 'image', 'review'):
        log.add_argument('--' + option, action='append', default=[])
    serving = commands.add_parser('serve')
    serving.add_argument('--port', type=int, default=0)
    args = parser.parse_args()
    if args.command == 'compare':
        print(json.dumps(compare(args.before, args.after, args.build, args.batch, args.classification, args.cause)))
    elif args.command == 'journal':
        print(journal(args.build, args.phase, args.message, args.file, args.test, args.image, args.review))
    else:
        with server(args.build, args.port) as http:
            origin = f'http://127.0.0.1:{http.server_port}'
            print('Owned review server. Stop with Ctrl+C.', flush=True)
            for path in ('screenshots/index.html', 'screenshots/ui-review.html', 'refactor-journal/index.html'):
                print(origin + '/' + path, flush=True)
            try:
                http.serve_forever()
            except KeyboardInterrupt:
                pass


if __name__ == '__main__':
    main()
