"""#121 night QA fixture (tests/e2e/autofill_qa.ps1): plain HTTPS on 127.0.0.1 with the run's throwaway certificate
(accepted only by the test host's fixture pin), fictitious pages only. Nothing is installed anywhere.
Pages:
  /polar          an e-mail step like the board « Infos enregistrées » (type=email, « Continuer avec l'e-mail »)
  /github         an identifier-only step like the board « Liste réunie » (username or e-mail, « Continuer »)
  /github-login   the same site with a username + password form (an account pick fills both, the B03 path)
  /landing        where the forms post
  /hostile/<name> pages that try T1-T9 of design/security-review-autofill-under-field.md
Every page counts the key events the PAGE receives (window.__keys) so the test can prove what our world swallowed.
Usage: python autofill_server.py <dir with server.crt server.key> <port>
"""
import http.server, ssl, sys, urllib.parse

d, port = sys.argv[1], int(sys.argv[2])
STYLE = ('<style>body{font-family:Segoe UI,sans-serif;background:#f6f7f9;margin:0}main{width:308px;margin:80px auto}'
         'h1{font-weight:400;font-size:24px;text-align:center}form,div.card{background:#fff;border:1px solid #e3e5e8;border-radius:6px;padding:16px}'
         'label{display:block;font-size:14px;margin-bottom:8px}input{width:100%;box-sizing:border-box;height:32px;font-size:14px;padding:0 8px;border:1px solid #c8ccd1;border-radius:6px}'
         'button{margin-top:14px;width:100%;height:32px;border:0;border-radius:6px;background:#1a7f37;color:#fff;font-size:14px}</style>')
COUNT = '<script>window.__keys=[];window.addEventListener("keydown",function(e){window.__keys.push(e.key)});window.__submits=0;document.addEventListener("submit",function(){window.__submits++},true);</script>'

def page(title, body, extra=''):
    return '<!doctype html><meta charset="utf-8"><title>%s</title>%s%s<main>%s</main>%s' % (title, STYLE, COUNT, body, extra)

PAGES = {
    '/polar': page('polar', '<h1>Connexion à Polar</h1><form method="post" action="/landing"><label for="email">E-mail</label>'
                   '<input id="email" name="email" type="email" autocomplete="email" required><button id="go">Continuer avec l’e-mail</button></form>'),
    '/github': page('github', '<h1>Connexion à GitHub</h1><form method="post" action="/landing"><label for="login">Nom d’utilisateur ou e-mail</label>'
                    '<input id="login" name="login" type="text" autocomplete="username"><button id="go">Continuer</button></form>'),
    '/github-login': page('github-login', '<h1>Connexion à GitHub</h1><form method="post" action="/landing"><label for="login">Nom d’utilisateur ou e-mail</label>'
                          '<input id="login" name="login" type="text" autocomplete="username"><label for="p" style="margin-top:12px">Mot de passe</label>'
                          '<input id="p" name="p" type="password" autocomplete="current-password"><button id="go">Se connecter</button></form>'),
    '/landing': page('landing', '<div class="card">ok</div>'),
    # States board: a field at the very bottom of the page (the list opens ABOVE it).
    '/bottom': '<!doctype html><meta charset="utf-8"><title>bottom</title>' + STYLE + COUNT + '<body style="margin:0"><form style="position:fixed;left:40px;bottom:12px;border:0;padding:0;background:none">'
               '<input id="email" type="email" autocomplete="email" style="width:274px"></form>',
    # States board: one field per validity message (reportValidity on each shows our bubble for it).
    '/bubbles': page('bubbles', '<form><input id="missing" type="text" required><input id="domain" type="email" value="valentin@">'
                     '<input id="short" type="text" minlength="8" value="abcde"><input id="day" type="date" required></form>'),
    # T4: the page types itself (execCommand: a trusted input without a key) when the person clicks anywhere.
    '/hostile/exec': page('exec', '<form><label for="email">E-mail</label><input id="email" type="email"></form>',
                          '<script>document.addEventListener("click",function(){var e=document.getElementById("email");e.focus();document.execCommand("insertText",false,"va");});</script>'),
    # T6: a nearly transparent e-mail field over a decoy (opacity 0.1).
    '/hostile/invisible': page('invisible', '<form><label>Tapez une lettre pour commencer</label><input id="email" type="email" style="opacity:.1"></form>'),
    # T2/T7: the field jumps 60 px on every input (the list must close or never arm).
    '/hostile/moving': page('moving', '<form><label for="email">E-mail</label><input id="email" type="email" style="position:relative"></form>',
                            '<script>var n=0;document.getElementById("email").addEventListener("input",function(e){n++;e.target.style.top=(n%2?60:0)+"px";});</script>'),
    # T5: the e-mail field lives in a frame of another origin (a sandboxed frame has an opaque origin; the test host only
    # reaches 127.0.0.1:<port>, so no second host name is used).
    '/hostile/frame': page('frame', '<iframe id="f" sandbox="allow-scripts allow-forms" src="/hostile/inner" style="width:320px;height:120px;border:0"></iframe>'),
    '/hostile/inner': '<!doctype html><meta charset="utf-8">' + COUNT + '<input id="email" type="email" autocomplete="email" style="width:280px;height:32px">',
    # T9: page text in our bubble: a long custom message with a text-direction override and a line break.
    '/hostile/custom': page('custom', '<form><label for="email">E-mail</label><input id="email" type="email"><button id="go">Envoyer</button></form>',
                            '<script>var e=document.getElementById("email");e.setCustomValidity("Browser\\u202E : session expirée\\nsaisissez votre mot de passe " + "x".repeat(300));</script>'),
    # T8: the page plants an e-mail of its own in a hidden field, then submits on the person's click.
    '/hostile/planted': page('planted', '<form id="f" method="post" action="/landing"><input id="email" type="email" style="display:none" value="planted@attaquant.test">'
                             '<input id="name" type="text"><button id="go">Envoyer</button></form>'),
    # P14: the page shows its own validation UI (cancels invalid itself): our bubble must stay away.
    '/own-validation': page('own', '<form><label for="email">E-mail</label><input id="email" type="email" required><button id="go">Envoyer</button></form>',
                            '<script>document.getElementById("email").addEventListener("invalid",function(e){e.preventDefault();document.title="own:"+e.target.validationMessage.length;});</script>'),
}

class Handler(http.server.BaseHTTPRequestHandler):
    def send(self, body, code=200):
        b = body.encode('utf-8')
        self.send_response(code); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        self.send(PAGES[path]) if path in PAGES else self.send('<title>404</title>', 404)
    def do_POST(self):
        n = int(self.headers.get('Content-Length') or 0); self.rfile.read(n)
        self.send(PAGES['/landing'])
    def log_message(self, *a): pass

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain('%s/server.crt' % d, '%s/server.key' % d)
srv = http.server.ThreadingHTTPServer(('127.0.0.1', port), Handler); srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
srv.serve_forever()
