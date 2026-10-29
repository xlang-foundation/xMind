"""Browser transport hosted by xmind.exe's embedded xlang3 runtime.

The native binding owns workspace routing, admission and every domain action.
This module owns only HTTP, browser cookies, assets and ASGI event delivery.
"""
import asyncio
import json
import re
import socket
from pathlib import Path

from fastapi import FastAPI, Request
from starlette.responses import JSONResponse, Response, StreamingResponse
import uvicorn
import h11
from uvicorn.protocols.http.h11_impl import H11Protocol, DEFAULT_MAX_INCOMPLETE_EVENT_SIZE

ASSETS = {
    '': ('index.html', 'text/html'),
    **{name: (name, 'text/javascript') for name in (
        'browser.js', 'terminal.js', 'chat.js', 'client.js', 'marked.js',
        'purify.js', 'patch-review.js', 'xterm.js', 'xterm-fit.js', 'layout.js')},
    **{name: (name, 'text/css') for name in (
        'browser.css', 'chat.css', 'xterm.css', 'layout.css')},
}
CSP = ("default-src 'none'; script-src 'self'; style-src 'self'; "
       "connect-src 'self'; img-src 'self' data:; base-uri 'none'; "
       "form-action 'none'; frame-ancestors 'none'; object-src 'none'")
SESSION = re.compile(r'[0-9a-f]{64}\.[0-9a-f]{64}')
WORKSPACE_ROUTES = {'', '/select', '/create', '/add', '/pick', '/rename'}


class RequestTooLarge(Exception):
    pass


class NativeResponseInvalid(Exception):
    pass


class InvalidViewSession(NativeResponseInvalid):
    pass


class TargetConnection(h11.Connection):
    """Retain h11's parsed request target, including an empty query marker."""
    target = None

    def next_event(self):
        event = super().next_event()
        if isinstance(event, h11.Request):
            self.target = event.target
        return event


class NativeHttpProtocol(H11Protocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        limit = self.config.h11_max_incomplete_event_size
        self.conn = TargetConnection(h11.SERVER, limit or DEFAULT_MAX_INCOMPLETE_EVENT_SIZE)

    def handle_events(self):
        super().handle_events()
        # The ASGI task runs on the next event-loop iteration. Preserve the
        # parser's target before it starts, rather than parsing HTTP twice.
        if self.scope is not None and self.conn.target is not None:
            self.scope['xmind.raw_target'] = self.conn.target


def read_assets(root):
    directory = Path(root).resolve(strict=True)
    result = {}
    total = 0
    for route, (name, mime) in ASSETS.items():
        path = directory / name
        if path.resolve(strict=True) != path:
            raise ValueError('Browser assets must remain in their configured directory')
        data = path.read_bytes()
        total += len(data)
        if len(data) > 2 * 1024 * 1024 or total > 8 * 1024 * 1024:
            raise ValueError('Browser assets exceed limits')
        result['/ui/' + route] = (data, mime)
    return result


API_ROUTES = {method: tuple(re.compile(pattern) for pattern in patterns)
              for method, patterns in json.loads(
                  Path(__file__).with_name('xmind_web_routes.json').read_text(encoding='utf-8')).items()}


def api_allowed(path, method):
    return any(pattern.fullmatch(path) for pattern in API_ROUTES.get(method, ()))


def valid_query(path, method, query, has_query):
    """Bound the browser observation contract before native dispatch."""
    def shape(keys):
        return all(key in keys and len(query.getlist(key)) == 1 for key in query)

    def text(key, limit):
        return key not in query or 0 < len(query[key]) <= limit

    def number(key, maximum, zero=False):
        return key not in query or bool(re.fullmatch(r'0|[1-9][0-9]*' if zero else r'[1-9][0-9]*', query[key])) and int(query[key]) <= maximum

    if re.fullmatch(r'/v1/sessions/[A-Za-z0-9_-]+/messages', path):
        return (not has_query or bool(query)) and shape({'limit', 'before'}) and number('limit', 1000) and ('before' not in query or bool(re.fullmatch(r'msg_[1-9][0-9]*', query['before'])) and int(query['before'][4:]) <= 9007199254740991)
    if re.fullmatch(r'/v1/sessions/[A-Za-z0-9_-]+/messages/[A-Za-z0-9_-]+', path):
        return not has_query
    if path in ('/v1/workspace/files', '/v1/workspace/file'):
        return shape({'path'}) and text('path', 32768)
    if path in ('/v1/workspace/folders/files', '/v1/workspace/folders/file', '/v1/workspace/find', '/v1/workspace/folders/find'):
        folder = '/folders/' in path
        find = path.endswith('/find')
        keys = {'path'} | ({'folder_id'} if folder else set()) | ({'query', 'type', 'limit', 'hidden', 'respect_ignore'} if find else set())
        if not shape(keys) or not text('path', 32768):
            return False
        if folder and ('folder_id' not in query or not re.fullmatch(r'windows-local-file-v1:[A-Za-z0-9:._-]{1,200}', query['folder_id'])):
            return False
        if not find:
            return not path.endswith('/file') or 'path' in query
        return 'query' in query and text('query', 4096) and ('type' not in query or query['type'] in ('file', 'directory')) and number('limit', 1000) and all(key not in query or query[key] in ('true', 'false') for key in ('hidden', 'respect_ignore'))
    if re.fullmatch(r'/v1/(?:connection-bindings/[a-f0-9]{32}/)?sessions/[A-Za-z0-9_-]+/context(?:/requests/[A-Za-z0-9_-]+)?', path):
        return shape({'model_id'}) and ('model_id' not in query or bool(re.fullmatch(r'[A-Za-z0-9_.:/-]{1,256}', query['model_id'])))
    if re.fullmatch(r'/v1/operations/[A-Za-z0-9_-]+/output', path):
        return set(query) == {'channel', 'after', 'limit'} and shape({'channel', 'after', 'limit'}) and query['channel'] in ('stdout', 'stderr') and number('after', 64 * 1024 * 1024, zero=True) and number('limit', 262144)
    if method == 'POST' or path.startswith('/v1/mcp/authorization/') or path in ('/v1/mcp/resources', '/v1/agent/planning') or re.fullmatch(r'/v1/runs/[A-Za-z0-9_-]+/plan(?:/.*)?', path):
        return not has_query
    return shape({'after'}) and ('after' not in query or bool(re.fullmatch(r'[0-9]+', query['after'])))


class BrowserApp:
    def __init__(self, native, origin, asset_root):
        self.native = native
        self.origin = origin
        self.host = origin.removeprefix('http://')
        self.cookie_name = 'xmind_view_' + origin.rsplit(':', 1)[1]
        self.asset_root = asset_root
        self.assets = read_assets(asset_root)
        self.app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None)
        self.app.add_api_route('/{path:path}', self.handle,
                               methods=['GET', 'HEAD', 'POST', 'DELETE', 'PUT', 'PATCH', 'OPTIONS'])

    def cookie(self, request):
        values = [part.strip().split('=', 1)[1]
                  for part in request.headers.get('cookie', '').split(';')
                  if part.strip().startswith(self.cookie_name + '=')]
        return values[0] if len(values) == 1 and SESSION.fullmatch(values[0]) else None

    @staticmethod
    def reply(status, value):
        return JSONResponse(value, status_code=status,
                            headers={'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff'})

    async def body(self, request, limit):
        result = bytearray()
        async for chunk in request.stream():
            result.extend(chunk)
            if len(result) > limit:
                raise RequestTooLarge('Request exceeds limits')
        return bytes(result).decode('utf-8')

    async def invoke(self, method, target, body='', credential=None, owner=False, last_event='', authorization=None):
        headers = {'Content-Type': 'application/json'}
        if credential:
            headers['Authorization'] = 'View ' + credential
            headers['X-XMind-View-Origin'] = self.origin
        elif authorization:
            headers['Authorization'] = authorization
        if last_event:
            headers['Last-Event-ID'] = last_event
        # Production returns a native job immediately. The external-peer test
        # adapter retains its blocking Request interface on an executor thread.
        if hasattr(self.native, 'RequestAsync'):
            result = self.native.RequestAsync(method, target, headers, body, owner)
        else:
            result = await asyncio.to_thread(self.native.Request, method, target, headers, body, owner)
        try:
            while not result.Ready():
                await asyncio.sleep(0.001)
            return result
        except BaseException:
            result.Close()
            raise

    def set_cookie(self, response, value):
        if type(value) is not dict:
            raise InvalidViewSession('Invalid native browser session')
        credential = value.get('credential', '')
        age = value.get('max_age_seconds')
        expiry = value.get('expires_unix_ms')
        if not SESSION.fullmatch(credential) or type(age) is not int or not 1 <= age <= 28800 or type(expiry) is not int or expiry < 1:
            raise InvalidViewSession('Invalid native browser session')
        response.set_cookie(self.cookie_name, credential, max_age=age, httponly=True,
                            samesite='strict', path='/')
        return response

    async def json_call(self, method, target, body='', credential=None, owner=False, authorization=None):
        result = await self.invoke(method, target, body, credential, owner, authorization=authorization)
        try:
            try:
                value = json.loads(result.Body())
            except (ValueError, UnicodeError) as error:
                raise NativeResponseInvalid('Invalid native JSON response') from error
            return result.Status(), value
        finally:
            result.Close()

    async def handle(self, request: Request):
        path = request.url.path
        raw_target = request.scope.get('xmind.raw_target')
        has_query = b'?' in raw_target if raw_target is not None else bool(request.url.query)
        origin = request.headers.get('origin')
        same_origin = origin == self.origin
        if request.headers.getlist('host') != [self.host]:
            return self.reply(400, {'detail': 'Invalid view host'})
        if len(request.headers.getlist('origin')) > 1:
            return self.reply(403, {'detail': 'View origin rejected'})
        if request.scope.get('raw_path', path.encode()) != path.encode('ascii', errors='replace'):
            return self.reply(404, {'detail': 'View route not found'})
        if (origin is not None and not same_origin) or request.headers.get('sec-fetch-site') in ('cross-site', 'same-site'):
            return self.reply(403, {'detail': 'View origin rejected'})
        if request.method in ('POST', 'DELETE', 'PUT', 'PATCH') and (not same_origin or request.headers.get('content-type') not in (('application/json', None) if request.method == 'DELETE' else ('application/json',))):
            return self.reply(403, {'detail': 'Same-origin JSON access required'})
        if request.method == 'OPTIONS':
            return Response(status_code=204, headers={'Allow': 'GET, HEAD, POST, DELETE, OPTIONS'})
        try:
            credential = self.cookie(request)
            authorization = request.headers.get('authorization')
            if authorization is not None and (len(request.headers.getlist('authorization')) != 1 or not re.fullmatch(r'Bearer [\x21-\x7e]{32,256}', authorization)):
                return self.reply(401, {'detail': 'Invalid native owner authentication'})
            if path in ('/ui/session', '/ui/session/disconnect'):
                if request.method != 'POST' or has_query:
                    return self.reply(403, {'detail': 'Same-origin session access required'})
                await self.body(request, 4096)
                if path.endswith('/disconnect'):
                    if credential:
                        status, _ = await self.json_call('POST', '/v1/view-sessions/revoke', '{}', credential)
                        if status not in (200, 401):
                            return self.reply(502, {'detail': 'Session revocation was not confirmed'})
                    response = self.reply(200, {'connected': False})
                    response.delete_cookie(self.cookie_name, path='/', httponly=True, samesite='strict')
                    return response
                if credential and not authorization:
                    status, _ = await self.json_call('POST', '/v1/view-sessions/current', '{}', credential)
                    if status == 200:
                        return self.reply(200, {'connected': True})
                    if status != 401:
                        return self.reply(502, {'connected': False})
                status, value = await self.json_call('POST', '/v1/view-sessions', json.dumps({'origin': self.origin}), owner=authorization is None, authorization=authorization)
                if status != 200:
                    return self.reply(429 if status == 409 else 401 if status == 401 else 502, {'connected': False})
                return self.set_cookie(self.reply(200, {'connected': True}), value)
            if path in self.assets and request.method in ('GET', 'HEAD'):
                if path == '/ui/':
                    self.assets = read_assets(self.asset_root)
                data, mime = self.assets[path]
                return Response(data if request.method == 'GET' else b'', media_type=mime,
                                headers={'Content-Security-Policy': CSP, 'Cache-Control': 'no-store',
                                         'Content-Length': str(len(data)),
                                         'X-Content-Type-Options': 'nosniff', 'X-Frame-Options': 'DENY',
                                         'Referrer-Policy': 'no-referrer'})
            if not path.startswith('/ui/workspaces') and not api_allowed(path, request.method):
                return self.reply(404, {'detail': 'View route not found'})
            if not authorization and (not credential or request.headers.get('sec-fetch-site') != 'same-origin'):
                return self.reply(401, {'detail': 'Open this browser view from local xMind'})
            if path.startswith('/ui/workspaces'):
                if not credential:
                    return self.reply(401, {'detail': 'A browser session is required'})
                suffix = path[len('/ui/workspaces'):]
                if suffix == '/sessions':
                    query = request.query_params
                    if request.method != 'GET' or set(query) != {'workspace_id'} or len(query.getlist('workspace_id')) != 1 or not re.fullmatch(r'windows-local-file-v1:[A-Za-z0-9:._-]{1,200}', query['workspace_id']):
                        raise ValueError('Choose one registered workspace')
                    status, value = await self.json_call('GET', '/v1/workspaces/sessions?workspace_id=' + query['workspace_id'], credential=credential)
                    return self.reply(status, value)
                if suffix not in WORKSPACE_ROUTES or has_query or request.method != ('POST' if suffix else 'GET'):
                    return self.reply(404, {'detail': 'Workspace route not found'})
                body = await self.body(request, 4096) if suffix else ''
                if suffix:
                    value = json.loads(body)
                    keys = set(value) if type(value) is dict else None
                    expected = {'/pick': [set()], '/select': [{'workspace_id'}],
                                '/create': [{'root'}, {'root', 'name'}],
                                '/add': [{'workspace_id', 'root'}],
                                '/rename': [{'workspace_id', 'name', 'expected_name'}]}
                    if keys not in expected[suffix]:
                        raise ValueError('Invalid workspace request fields')
                    for key, field in value.items():
                        if type(field) is not str or not field or len(field) > 32760:
                            raise ValueError('Invalid workspace request field')
                        if key == 'workspace_id' and not re.fullmatch(r'windows-local-file-v1:[A-Za-z0-9:._-]{1,200}', field):
                            raise ValueError('Invalid workspace identity')
                        if key in ('name', 'expected_name') and (not field.strip() or len(field.encode()) > 1024 or any(ord(char) < 32 or ord(char) == 127 for char in field)):
                            raise ValueError('Invalid workspace name')
                status, value = await self.json_call(request.method, '/v1/workspaces' + suffix, body, credential)
                if status == 200 and suffix in ('/create', '/select'):
                    response = self.reply(200, {'connected': True, 'workspace': value['workspace']})
                    return self.set_cookie(response, value)
                return self.reply(status, value)
            if not api_allowed(path, request.method):
                return self.reply(404, {'detail': 'View route not found'})
            if not valid_query(path, request.method, request.query_params, has_query):
                raise ValueError('Invalid view observation query')
            body = await self.body(request, 4 * 1024 * 1024 if path == '/v1/sessions/import' else 1024 * 1024)
            if request.method == 'DELETE' and body and request.headers.get('content-type') != 'application/json':
                return self.reply(403, {'detail': 'Same-origin JSON access required'})
            target = raw_target.decode('ascii') if raw_target is not None else request.scope.get('raw_path', path.encode()).decode('ascii')
            if raw_target is None and request.url.query:
                target += '?' + request.url.query
            result = await self.invoke(request.method, target, body, credential,
                                       last_event=request.headers.get('last-event-id', ''), authorization=authorization)
            if result.Streaming() and result.Status() == 200:
                async def events():
                    try:
                        while True:
                            value = await asyncio.to_thread(result.Read)
                            if value['data']:
                                yield value['data']
                            if value['done']:
                                break
                    finally:
                        # An ASGI disconnect cancels the response's task group.
                        # Keep native reader cleanup alive outside that scope.
                        cleanup = asyncio.create_task(asyncio.to_thread(result.Close))
                        await asyncio.shield(cleanup)
                return StreamingResponse(events(), media_type='text/event-stream',
                                         headers={'Content-Type': 'text/event-stream', 'Cache-Control': 'no-store', 'X-Accel-Buffering': 'no'})
            try:
                status = result.Status()
                data = result.Body()
                if status == 204:
                    return Response(status_code=204)
                if len(data.encode()) > 16 * 1024 * 1024:
                    raise NativeResponseInvalid('Native response exceeds limits')
                try:
                    value = json.loads(data)
                except (ValueError, UnicodeError) as error:
                    raise NativeResponseInvalid('Invalid native JSON response') from error
                return self.reply(status, value)
            finally:
                result.Close()
        except InvalidViewSession:
            return self.reply(502, {'error_code': 'invalid_view_session', 'detail': 'The server returned an invalid access session. Update the server and browser adapter together.'})
        except RequestTooLarge:
            return self.reply(413, {'detail': 'Browser request exceeds limits'})
        except (ValueError, UnicodeError):
            return self.reply(400, {'detail': 'Invalid browser request'})
        except Exception:
            return self.reply(502, {'detail': 'Native request unavailable; no replay was attempted'})


_listener = None


def prepare(port=0):
    global _listener
    _listener = socket.socket()
    _listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    _listener.bind(('127.0.0.1', port))
    return _listener.getsockname()[1]


def serve(native, origin, asset_root):
    browser = BrowserApp(native, origin, asset_root)
    server = uvicorn.Server(uvicorn.Config(browser.app, host='127.0.0.1',
                            loop='asyncio', http=NativeHttpProtocol, ws='none', lifespan='off',
                            proxy_headers=False, timeout_keep_alive=1, timeout_graceful_shutdown=10,
                            log_level='warning', access_log=False))
    async def run():
        async def watch_backend():
            while native.Running():
                await asyncio.sleep(0.1)
            server.should_exit = True
        watcher = asyncio.create_task(watch_backend())
        try:
            await server.serve(sockets=[_listener])
        finally:
            watcher.cancel()
            await asyncio.gather(watcher, return_exceptions=True)
    asyncio.run(run())
    return True
