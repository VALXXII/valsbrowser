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
        cert = self.connection.getpeercert() or {}
        subject = ', '.join('='.join(x) for rdn in cert.get('subject', ()) for x in rdn)
        with open(os.path.join(d, 'observed.txt'), 'a', encoding='utf-8') as f: f.write(subject + '\n')
        body = ('<title>mtls ok</title><p>client: %s</p>' % subject).encode('utf-8')
        self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    def log_message(self, *a): pass

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(os.path.join(d, 'server.crt'), os.path.join(d, 'server.key'))
if client_auth:
    ctx.load_verify_locations(os.path.join(d, 'ca.crt'))
    ctx.verify_mode = ssl.CERT_REQUIRED
srv = http.server.HTTPServer(('127.0.0.1', port), Handler)
srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
srv.serve_forever()
