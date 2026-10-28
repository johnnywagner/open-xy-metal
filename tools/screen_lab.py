#!/usr/bin/env python3
"""Metal's screen lab: the SDK's workbench with a panel of screen-only settings added. Browser only; nothing here
reaches the OP-XY. Run from the repository root, with a preview built by `sdk.py preview`:

    python3 tools/screen_lab.py --preview /path/to/preview

then open http://127.0.0.1:8918/. It serves exactly what the SDK's tools/serve_harness.py serves (imported from the
SDK beside this repository, not changed), plus tools/screen_lab/ under /lab/, and adds the lab's script and style to
the workbench page. The panel's settings stay in the browser (localStorage).
"""
import argparse
import importlib.util
from pathlib import Path
from urllib.parse import unquote, urlsplit

REPO = Path(__file__).resolve().parents[1]
LAB = REPO / 'tools' / 'screen_lab'
PAGES = ('/', '/harness/', '/harness/index.html')
APP = '<script type="module" src="/harness/app.mjs"></script>'
ADDED = '<link rel="stylesheet" href="/lab/lab.css"><script type="module" src="/lab/lab.mjs"></script>'

def load_harness(sdk):
    spec = importlib.util.spec_from_file_location('serve_harness', sdk / 'tools' / 'serve_harness.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def lab_handler(harness):
    class Handler(harness.Handler):
        def respond(self, head):
            path = unquote(urlsplit(self.path).path)
            if path in PAGES:
                page = harness.resolve_public_path(path).read_text(encoding='utf-8')
                if APP not in page:
                    self.send_error(500, 'The workbench page changed; the lab cannot add its panel')
                    return
                self.send_data('text/html; charset=utf-8', page.replace(APP, ADDED + APP).encode(), head)
            elif path.startswith('/lab/'):
                file = LAB / path[len('/lab/'):]
                if file.parent != LAB or file.suffix not in harness.MIME or not file.is_file():
                    self.send_error(404)
                    return
                self.send_data(harness.MIME[file.suffix] + '; charset=utf-8', file.read_bytes(), head)
            else:
                super().respond(head)

        def send_data(self, content_type, data, head):
            # The workbench's own headers, its content security policy included.
            self.send_response(200)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'")
            self.end_headers()
            if not head:
                self.wfile.write(data)
    return Handler

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--preview', type=Path, required=True, help='Fresh C++ preview built by sdk.py preview')
    parser.add_argument('--sdk', type=Path, default=REPO.parent / 'open-xy', help='The SDK (default ../open-xy)')
    parser.add_argument('--port', type=int, default=8918)
    args = parser.parse_args()
    harness = load_harness(args.sdk.resolve())
    bundle = harness.preview_bundle(args.preview.resolve())
    try:
        server = harness.PreviewServer(('127.0.0.1', args.port), lab_handler(harness))
    except OSError as error:
        parser.exit(1, f'Port {args.port} is unavailable ({error.strerror or error}). '
                       f'Stop the other server or pass --port {args.port + 20}.\n')
    server.preview = bundle
    print(f'Metal screen lab: http://127.0.0.1:{server.server_port}', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
