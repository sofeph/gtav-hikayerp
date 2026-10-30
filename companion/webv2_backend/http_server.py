"""Loopback-only HTTP host for WebV2 pages and Python plugin routes."""

import json
import logging
import mimetypes
import threading
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlsplit

from .engine import Request, Response

log = logging.getLogger('webv2.http')


class WebV2HTTPServer:
    def __init__(self, engine, web_root, host='127.0.0.1', port=8766):
        if host not in ('127.0.0.1', 'localhost', '::1'):
            raise ValueError('WebV2 HTTP server is restricted to loopback interfaces.')
        self.engine = engine
        self.web_root = Path(web_root).resolve()
        self._server = None
        self._thread = None
        owner = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = 'HTTP/1.1'

            def do_GET(self):
                owner._dispatch(self)

            def do_HEAD(self):
                owner._dispatch(self, head_only=True)

            def do_POST(self):
                owner._dispatch(self)

            def do_PUT(self):
                owner._dispatch(self)

            def do_DELETE(self):
                owner._dispatch(self)

            def log_message(self, fmt, *args):
                log.info('%s - %s', self.address_string(), fmt % args)

        self._handler = Handler
        self.host = host
        self.port = int(port)

    def start(self):
        if self._server is not None:
            return self
        self._server = ThreadingHTTPServer((self.host, self.port), self._handler)
        self._server.daemon_threads = True
        self._thread = threading.Thread(target=self._server.serve_forever, name='webv2-http', daemon=True)
        self._thread.start()
        return self

    def close(self):
        if self._server is not None:
            self._server.shutdown()
            self._server.server_close()
            self._server = None
            self._thread = None

    def _dispatch(self, handler, head_only=False):
        parts = urlsplit(handler.path)
        path = unquote(parts.path)
        if path == '/':
            handler.send_response(302)
            handler.send_header('Location', '/webv2/pc/home.html')
            handler.send_header('Content-Length', '0')
            handler.end_headers()
            return
        if path.startswith('/webv2/'):
            self._serve_static(handler, path[len('/webv2/'):], head_only)
            return
        if path == '/api/tv/channels' and handler.command in ('GET', 'HEAD'):
            response = Response().json(self.engine.tv_manager.all_channels())
            self._send_response(handler, response, head_only)
            return

        length = int(handler.headers.get('Content-Length', '0') or 0)
        if length < 0 or length > 1024 * 1024:
            self._send_response(handler, Response(status=413).text('Request body too large.'), head_only)
            return
        body = handler.rfile.read(length).decode('utf-8', errors='replace') if length else ''
        cookies = SimpleCookie()
        cookies.load(handler.headers.get('Cookie', ''))
        request = Request(
            method=handler.command,
            path=path,
            query={key: values[-1] for key, values in parse_qs(parts.query, keep_blank_values=True).items()},
            headers=dict(handler.headers.items()),
            body=body,
            cookies={key: morsel.value for key, morsel in cookies.items()},
            client_ip=handler.client_address[0],
        )
        response = self.engine.handle_request(request)
        self._send_response(handler, response, head_only)

    def _serve_static(self, handler, relative, head_only):
        candidate = (self.web_root / relative).resolve()
        try:
            candidate.relative_to(self.web_root)
        except ValueError:
            self._send_response(handler, Response(status=404).text('Not found.'), head_only)
            return
        if not candidate.is_file():
            self._send_response(handler, Response(status=404).text('Not found.'), head_only)
            return
        try:
            payload = candidate.read_bytes()
        except OSError:
            log.exception('WebV2 static file read failed: %s', candidate)
            self._send_response(handler, Response(status=500).text('File read failed.'), head_only)
            return
        content_type, _ = mimetypes.guess_type(candidate.name)
        response = Response(status=200, body=payload.decode('latin-1'))
        response.headers['Content-Type'] = content_type or 'application/octet-stream'
        self._send_response(handler, response, head_only, raw=payload)

    @staticmethod
    def _send_response(handler, response, head_only=False, raw=None):
        body = raw if raw is not None else response.body.encode('utf-8')
        handler.send_response(response.status)
        for key, value in response.headers.items():
            handler.send_header(key, value)
        for name, cookie in response.cookies.items():
            handler.send_header('Set-Cookie', f"{name}={cookie['value']}; Max-Age={int(cookie.get('max_age', 3600))}; Path=/; HttpOnly; SameSite=Lax")
        handler.send_header('X-Content-Type-Options', 'nosniff')
        handler.send_header('Content-Length', str(len(body)))
        handler.end_headers()
        if not head_only:
            handler.wfile.write(body)