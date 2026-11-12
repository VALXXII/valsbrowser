"""Local HTTP fixture for tests/e2e/http_status_e2e.ps1 (fixture only, 127.0.0.1, nothing installed).
/s/<code>  : a full HTML body with a title and an inline JS sentinel, served with HTTP status <code>
             (200, 403, 404, 429, 503: a challenge / error page a real site sends WITH its own body).
/cut       : headers announce 4096 bytes, 40 are sent, then the connection is closed (truncated response).
/empty     : the connection is closed without any response (empty reply).
/rst       : the connection is reset (RST) without any response.
/e/<code>  : HTTP status <code> with an empty body.
/cutc/<code>: HTTP status <code>, 4096 bytes announced, a partial body (title + JS sentinel) then the connection closes.
/r/<code>  : a 302 redirect to /s/<code>.
Usage: python http_status_server.py <port>
"""
import http.server, socket, struct, sys

port = int(sys.argv[1])

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def do_GET(self):
        if self.path.startswith('/s/'):
            code = int(self.path[3:6])
            body = ('<!doctype html><title>sentinel-%d</title><p id="body">corps %d du serveur</p>'
                    '<script>window.__sentinel="js-%d";</script>' % (code, code, code)).encode('utf-8')
            self.send_response(code); self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body))); self.send_header('Cache-Control', 'no-store'); self.end_headers()
            self.wfile.write(body); return
        if self.path.startswith('/e/'):   # an HTTP error status with an EMPTY body
            self.send_response(int(self.path[3:6])); self.send_header('Content-Length', '0'); self.send_header('Cache-Control', 'no-store'); self.end_headers(); return
        if self.path.startswith('/r/'):   # 302 to /s/<code>
            self.send_response(302); self.send_header('Location', '/s/' + self.path[3:6]); self.send_header('Content-Length', '0'); self.end_headers(); return
        if self.path.startswith('/cutc/'):   # status <code>, headers announce 4096 bytes, a PARTIAL body (title + sentinel script) then close
            code = int(self.path[6:9])
            part = ('<!doctype html><title>sentinel-cut%d</title><script>window.__sentinel="js-cut%d";</script><p>partiel' % (code, code)).encode('utf-8')
            self.send_response(code); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.send_header('Content-Length', '4096'); self.end_headers()
            self.wfile.write(part); self.wfile.flush()
            self.connection.shutdown(socket.SHUT_RDWR); self.close_connection = True; return
        if self.path.startswith('/cut'):
            self.send_response(200); self.send_header('Content-Type', 'text/html'); self.send_header('Content-Length', '4096'); self.end_headers()
            self.wfile.write(b'<!doctype html><title>sentinel-cut</title>'); self.wfile.flush()
            self.connection.shutdown(socket.SHUT_RDWR); self.close_connection = True; return
        if self.path.startswith('/empty'):
            self.connection.shutdown(socket.SHUT_RDWR); self.close_connection = True; return
        if self.path.startswith('/rst'):
            self.connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0)); self.connection.close(); self.close_connection = True; return
        self.send_response(404); self.send_header('Content-Length', '0'); self.end_headers()
    def log_message(self, *a): pass

http.server.ThreadingHTTPServer(('127.0.0.1', port), Handler).serve_forever()
