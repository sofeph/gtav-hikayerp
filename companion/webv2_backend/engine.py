"""Flask-style request, route, session, and template engine for WebV2."""

import json
import logging
import re
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

from .plugin import PluginManager
from .template import TemplateEngine
from .tv import TVChannelManager

log = logging.getLogger('webv2')


@dataclass
class Request:
    method: str = 'GET'
    path: str = '/'
    query: dict[str, str] = field(default_factory=dict)
    headers: dict[str, str] = field(default_factory=dict)
    body: str = ''
    cookies: dict[str, str] = field(default_factory=dict)
    session: dict[str, Any] = field(default_factory=dict)
    user: dict[str, Any] | None = None
    client_ip: str = '127.0.0.1'
    timestamp: float = field(default_factory=time.time)


@dataclass
class Response:
    status: int = 200
    headers: dict[str, str] = field(default_factory=dict)
    body: str = ''
    cookies: dict[str, dict[str, Any]] = field(default_factory=dict)

    def set_cookie(self, name, value, max_age=3600, httponly=True):
        self.cookies[name] = {'value': str(value), 'max_age': int(max_age), 'httponly': bool(httponly)}
        return self

    def delete_cookie(self, name):
        self.cookies[name] = {'value': '', 'max_age': 0, 'httponly': True}
        return self

    def json(self, data):
        self.body = json.dumps(data, ensure_ascii=False)
        self.headers['Content-Type'] = 'application/json; charset=utf-8'
        return self

    def html(self, content):
        self.body = str(content)
        self.headers['Content-Type'] = 'text/html; charset=utf-8'
        return self

    def text(self, content):
        self.body = str(content)
        self.headers['Content-Type'] = 'text/plain; charset=utf-8'
        return self


class HTTPError(Exception):
    def __init__(self, status, message=''):
        super().__init__(message)
        self.status = int(status)
        self.message = message


class WebV2Engine:
    """A small Flask-like app object intended for trusted local community plugins."""

    def __init__(self, root_path=None, data_root=None, plugin_dir=None):
        self.root_path = Path(root_path or Path.cwd()).resolve()
        self.data_root = Path(data_root or (self.root_path / 'data')).resolve()
        self.plugin_dir = Path(plugin_dir or (Path(__file__).parent / 'plugins')).resolve()
        self.routes: dict[str, list[tuple[re.Pattern, Callable]]] = {}
        self.route_definitions = []
        self.middleware: list[Callable] = []
        self.template_engine = TemplateEngine(Path(__file__).parent / 'templates')
        self.plugin_manager = PluginManager(self)
        self.tv_manager = TVChannelManager(self.data_root / 'webv2' / 'tv' / 'channels.json')
        self._sessions: dict[str, tuple[float, dict[str, Any]]] = {}
        self._session_ttl = 3600
        self._session_lock = __import__('threading').RLock()

    def route(self, path, methods=None):
        if not isinstance(path, str) or not path.startswith('/'):
            raise ValueError('Routes must begin with /.')
        methods = [method.upper() for method in (methods or ['GET'])]

        def decorator(handler):
            pattern, converters = self._compile_route(path)
            for method in methods:
                key = f'{method}:{path}'
                if any(existing_method == method and existing_path == path
                       for existing_method, existing_path, _ in self.route_definitions):
                    raise ValueError(f'Duplicate WebV2 route: {key}')
                entries = self.routes.setdefault(method, [])
                entries.append((pattern, lambda request, _handler=handler, _converters=converters, **kw:
                                _handler(request, **{name: converter(kw[name]) for name, converter in _converters.items()})))
                self.route_definitions.append((method, path, handler))
            return handler
        return decorator

    def get(self, path):
        return self.route(path, ['GET', 'HEAD'])

    def post(self, path):
        return self.route(path, ['POST'])

    def put(self, path):
        return self.route(path, ['PUT'])

    def delete(self, path):
        return self.route(path, ['DELETE'])

    @staticmethod
    def _compile_route(path):
        converters = {}
        pieces = []
        cursor = 0
        token = re.compile(r'<(?:(int|float|path):)?([A-Za-z_][A-Za-z0-9_]*)>')
        for match in token.finditer(path):
            pieces.append(re.escape(path[cursor:match.start()]))
            kind, name = match.groups()
            if name in converters:
                raise ValueError(f'Duplicate route variable: {name}')
            if kind == 'int':
                pieces.append(f'(?P<{name}>[0-9]+)')
                converters[name] = int
            elif kind == 'float':
                pieces.append(f'(?P<{name}>[0-9]+(?:\\.[0-9]+)?)')
                converters[name] = float
            elif kind == 'path':
                pieces.append(f'(?P<{name}>.+)')
                converters[name] = str
            else:
                pieces.append(f'(?P<{name}>[^/]+)')
                converters[name] = str
            cursor = match.end()
        pieces.append(re.escape(path[cursor:]))
        return re.compile('^' + ''.join(pieces) + '$'), converters

    def use(self, middleware):
        if not callable(middleware):
            raise TypeError('Middleware must be callable.')
        self.middleware.append(middleware)

    def handle_request(self, request):
        if request.method == 'HEAD':
            route_method = 'GET'
        else:
            route_method = request.method.upper()
        response = Response()
        self._load_session(request, response)
        try:
            for middleware in tuple(self.middleware):
                result = middleware(request)
                if isinstance(result, Response):
                    response = result
                    self._attach_session_cookie(request, response)
                    return response
            handler, params = self._find_route(route_method, request.path)
            if handler is None:
                response = Response(status=404).text('Not found.')
            else:
                result = handler(request, **params)
                if isinstance(result, Response):
                    response = result
                elif isinstance(result, dict):
                    response = Response().json(result)
                elif isinstance(result, str):
                    response = Response().html(result)
                elif result is None:
                    response = Response(status=204)
                else:
                    response = Response().text(str(result))
        except HTTPError as error:
            response = Response(status=error.status).text(error.message or 'Request failed.')
        except Exception:
            log.exception('WebV2 request failed: %s %s', request.method, request.path)
            response = Response(status=500).text('Internal server error.')
        self._attach_session_cookie(request, response)
        return response

    def _find_route(self, method, path):
        for pattern, handler in self.routes.get(method, []):
            match = pattern.match(path)
            if match:
                return handler, match.groupdict()
        return None, {}

    def _load_session(self, request, response):
        session_id = request.cookies.get('webv2_session', '')
        now = time.time()
        with self._session_lock:
            if len(self._sessions) > 1024:
                self._sessions = {key: value for key, value in self._sessions.items()
                                  if now - value[0] < self._session_ttl}
            stored = self._sessions.get(session_id)
            if stored and now - stored[0] < self._session_ttl:
                request.session = stored[1]
                self._sessions[session_id] = (now, stored[1])
                request._webv2_session_id = session_id
                return
            session_id = uuid.uuid4().hex
            request.session = {}
            request._webv2_session_id = session_id
            self._sessions[session_id] = (now, request.session)
            response.set_cookie('webv2_session', session_id, self._session_ttl)

    @staticmethod
    def _attach_session_cookie(request, response):
        session_id = getattr(request, '_webv2_session_id', None)
        if session_id:
            response.set_cookie('webv2_session', session_id)

    def render_template(self, template_name, context=None):
        values = dict(context or {})
        return self.template_engine.render(template_name, values)

    def load_plugins(self, plugin_dir=None):
        return self.plugin_manager.load_plugins(plugin_dir or self.plugin_dir)

    @staticmethod
    def abort(status, message=''):
        raise HTTPError(status, message)

    @staticmethod
    def redirect(location, status=302):
        response = Response(status=status)
        response.headers['Location'] = str(location)
        return response

    def url_for(self, endpoint, **values):
        for _, path, handler in self.route_definitions:
            if handler.__name__ == endpoint:
                for key, value in values.items():
                    path = path.replace(f'<{key}>', str(value))
                    path = path.replace(f'<int:{key}>', str(value))
                    path = path.replace(f'<float:{key}>', str(value))
                    path = path.replace(f'<path:{key}>', str(value))
                return path
        raise KeyError(f'Unknown WebV2 endpoint: {endpoint}')


def create_engine(root_path=None, data_root=None, plugin_dir=None):
    return WebV2Engine(root_path, data_root, plugin_dir)


_engine = None


def get_engine():
    global _engine
    if _engine is None:
        _engine = WebV2Engine()
    return _engine