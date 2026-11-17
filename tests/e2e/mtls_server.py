"""Local mTLS test server (fixture only). Serves https://127.0.0.1:<port>/ with a throwaway server
certificate, REQUIRES a client certificate issued by the run's own test CA (the CertificateRequest
names only that CA, so no personal identity matches), and records the client certificate subject it
actually received to <dir>/observed.txt. Nothing is installed anywhere.
Usage: python mtls_server.py <dir with server.crt server.key ca.crt> <port>
"""
import html, http.server, json, os, ssl, sys, urllib.parse

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
        if self.coverage_get():   # vault capture coverage E2E (tests/e2e/vault_coverage_e2e.ps1): fictitious accounts only
            return
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
    def send_html(self, html, csp=None):
        body = html.encode('utf-8')
        self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8')
        if csp: self.send_header('Content-Security-Policy', csp)
        self.send_header('Cache-Control', 'no-store'); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    # Synthetic accounts of the capture coverage E2E, named by a key in the URL (never a secret in a URL: URLs are logged).
    FILL = {'cadre': ('e2e-cov-cadre', 'E2e-Cadre-4004!'), 'autre': ('e2e-cov-autre', 'E2e-Autre-5005!'), 'sbx': ('e2e-cov-sbx', 'E2e-Sbx-7007!')}
    def coverage_get(self):
        # Capture coverage fixtures. Values to type come from the test (CDP), or from ?fill=<key> (FILL) for frames,
        # where the fixture page fills and submits itself (a submit event, like a person pressing the button).
        u = urllib.parse.urlsplit(self.path); q = urllib.parse.parse_qs(u.query, keep_blank_values=True); path = u.path
        def esc(v): return html.escape(v, quote=True)
        if path == '/spa':   # script-driven login: no <form>, a button / Enter, fetch(), then the field disappears or the page leaves
            mode = (q.get('mode') or ['gone'])[0]
            return self.send_html('<title>spa</title><div id="box"><input id="u" autocomplete="username"><input id="p" type="password">'
                '<button id="go" type="button">Se connecter</button></div><p id="msg"></p><script>'
                'function login(){fetch("/api/login",{method:"POST",body:"x"}).then(function(){'
                'if(%s){document.getElementById("msg").textContent="refuse";return;}'
                'if(%s){location.href="/home";return;}'
                'document.getElementById("box").remove();document.getElementById("msg").textContent="ok";});}'
                'document.getElementById("go").addEventListener("click",login);'
                'document.getElementById("p").addEventListener("keydown",function(e){if(e.key==="Enter")login();});'
                '</script>' % ('true' if mode == 'fail' else 'false', 'true' if mode == 'nav' else 'false')) or True
        if path == '/home':
            return self.send_html('<title>accueil</title><p>accueil</p>') or True
        if path == '/frame':   # a page that frames a login: same origin, another origin (?cross=<port>), or sandboxed (?sbx)
            fill = (q.get('fill') or [''])[0]
            src = ('https://127.0.0.1:%s' % q['cross'][0] if 'cross' in q else '') + '/login?fill=' + urllib.parse.quote(fill)
            sb = ' sandbox="allow-scripts allow-forms"' if 'sbx' in q else ''
            return self.send_html('<title>cadre</title><p>page</p><iframe id="f"%s src="%s" width="500" height="200"></iframe>' % (sb, esc(src))) or True
        if path == '/step1':   # two-step login, step 1: the username only
            return self.send_html('<title>etape 1</title><form method="post" action="/step2"><input id="u" name="u" autocomplete="username"><button id="go">Suivant</button></form>') or True
        if path == '/change':   # change-password form: current, new, confirm (?plain: no autocomplete hints; ?mismatch: confirm differs)
            plain = 'plain' in q
            ac = (lambda a: '') if plain else (lambda a: ' autocomplete="%s"' % a)
            return self.send_html('<title>changer</title><form method="post" action="/landing"><input id="u" name="u" autocomplete="username">'
                '<input id="cur" type="password"%s><input id="new" type="password"%s><input id="conf" type="password"%s><button id="go">Changer</button></form>'
                % (ac('current-password'), ac('new-password'), ac('new-password'))) or True
        if path == '/sbx-forge':   # R-6: an OPAQUE document served at the site's URL tries the old page channel, then submits
            fill = self.FILL.get((q.get('fill') or [''])[0], ('', ''))
            return self.send_html('<title>sbx forge</title><form method="post" action="/landing"><input id="u" name="u" autocomplete="username" value="%s"><input id="p" name="p" type="password" value="%s"><button id="go">go</button></form>'
                '<script>try{chrome.webview.postMessage({browserVault:"capture",user:document.getElementById("u").value,password:document.getElementById("p").value});}catch(e){}'
                'setTimeout(function(){document.getElementById("go").click();},400);</script>' % (esc(fill[0]), esc(fill[1] if len(fill) > 1 else '')), csp='sandbox allow-scripts allow-forms') or True
        if path == '/login' and 'fill' in q and q['fill'][0]:   # framed login that fills and submits itself
            fill = self.FILL.get(q['fill'][0], ('', ''))
            return self.send_html('<title>connexion</title><form method="post" action="/landing"><input id="u" name="u" autocomplete="username"><input id="p" name="p" type="password" autocomplete="current-password"><button id="go">go</button></form>'
                '<script>setTimeout(function(){document.getElementById("u").value=%s;document.getElementById("p").value=%s;document.getElementById("go").click();},600);</script>'
                % (json.dumps(fill[0]), json.dumps(fill[1] if len(fill) > 1 else ''))) or True
        return False
    def do_POST(self):   # vault E2E landing page: the body is read and dropped (never logged or stored)
        n = int(self.headers.get('Content-Length') or 0); self.rfile.read(n)
        if self.path.startswith('/step2'):   # two-step login, step 2: the password only, on a later page of the same origin
            return self.send_html('<title>etape 2</title><form method="post" action="/landing"><input id="p" name="p" type="password" autocomplete="current-password"><button id="go">Se connecter</button></form>')
        if self.path.startswith('/api/login'):
            body = b'{"ok":true}'
            self.send_response(200); self.send_header('Content-Type', 'application/json'); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body); return
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
