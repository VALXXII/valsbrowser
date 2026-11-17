"""Local mTLS test server (fixture only). Serves https://127.0.0.1:<port>/ with a throwaway server
certificate, REQUIRES a client certificate issued by the run's own test CA (the CertificateRequest
names only that CA, so no personal identity matches), and records the client certificate subject it
actually received to <dir>/observed.txt. Nothing is installed anywhere.
Usage: python mtls_server.py <dir with server.crt server.key ca.crt> <port>
"""
import http.server, os, ssl, sys

d, port = sys.argv[1], int(sys.argv[2])
client_auth = '--no-client-auth' not in sys.argv   # TLS-page E2E: plain HTTPS with an untrusted certificate

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        # Bound scenario helpers (test host must refuse any other port): a redirect and a sub-resource.
        if self.path.startswith('/redir?p='):
            self.send_response(302); self.send_header('Location', 'https://127.0.0.1:%s/r' % self.path.split('=')[1]); self.send_header('Content-Length', '0'); self.end_headers(); return
        if self.path.startswith('/with-img?p='):
            body = ('<title>with img</title><img src="https://127.0.0.1:%s/img.png">' % self.path.split('=')[1]).encode('utf-8')
            self.send_response(200); self.send_header('Content-Type', 'text/html'); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body); return
        if self.path.startswith('/sbx'):   # vault E2E (review R-1): an OPAQUE-origin document (CSP sandbox) served at the account's URL
            # ?spoof: the page's own script replaces window.self (a [Replaceable] attribute) so that self.origin
            # reads as the URL's origin, the bypass the independent review reproduced. The vault must not fill either.
            spoof = '<script>self={origin:location.origin};</script>' if 'spoof' in self.path else ''
            body = ('<title>sbx</title>%s<form method="post" action="/landing"><input id="u" name="u" autocomplete="username"><input id="p" name="p" type="password"><button id="go">go</button></form>' % spoof).encode('utf-8')
            self.send_response(200); self.send_header('Content-Type', 'text/html'); self.send_header('Content-Security-Policy', 'sandbox allow-scripts allow-forms'); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body); return
        if self.path.startswith('/login'):   # vault E2E: a synthetic login form (fictitious account only)
            # POST (the secret never goes into a URL); ?stay: the page keeps the submit (no navigation);
            # ?away: the form posts to another origin (localhost), which the test host refuses (navigation fails).
            action = 'https://localhost:%d/landing' % port if 'away' in self.path else '/landing'
            stay = ' onsubmit="event.preventDefault()"' if 'stay' in self.path else ''
            body = ('<title>connexion</title><form method="post" action="%s"%s><input id="u" name="u" autocomplete="username"><input id="p" name="p" type="password" autocomplete="current-password"><button id="go">go</button></form>' % (action, stay)).encode('utf-8')
            self.send_response(200); self.send_header('Content-Type', 'text/html'); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body); return
        cert = self.connection.getpeercert() or {}
        subject = ', '.join('='.join(x) for rdn in cert.get('subject', ()) for x in rdn)
        with open(os.path.join(d, 'observed.txt'), 'a', encoding='utf-8') as f: f.write(subject + '\n')
        body = ('<title>mtls ok</title><p>client: %s</p>' % subject).encode('utf-8')
        self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_POST(self):   # vault E2E landing page: the body is read and dropped (never logged or stored)
        n = int(self.headers.get('Content-Length') or 0); self.rfile.read(n)
        body = b'<title>connecte</title><p>ok</p>'
        self.send_response(200); self.send_header('Content-Type', 'text/html'); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    def log_message(self, *a): pass

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(os.path.join(d, 'server.crt'), os.path.join(d, 'server.key'))
if client_auth:
    ctx.load_verify_locations(os.path.join(d, 'ca.crt'))
    ctx.verify_mode = ssl.CERT_REQUIRED
srv = http.server.HTTPServer(('127.0.0.1', port), Handler)
srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
srv.serve_forever()
