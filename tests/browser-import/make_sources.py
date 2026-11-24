"""Synthetic browser profiles for the import E2E (tests/e2e/browser_import_e2e.ps1). Test-only: the browser
itself never uses Python. Writes, under <root>:
  Local/Google/Chrome/User Data/Default/{Bookmarks,History}   nested Unicode folders, duplicates, javascript:
  Local/Google/Chrome/User Data/Default/Favicons              icons of home.example and docs.example (real PNGs)
  Local/Microsoft/Edge/User Data/Profile 1/Bookmarks          a second Chromium browser (bookmarks only)
  Roaming/Mozilla/Firefox/profiles.ini + Profiles/e2e.default-release/places.sqlite (WAL, closed cleanly)
  export é#.html                                              Netscape bookmark export
Every URL is under *.example (RFC 2606) or localhost: no personal data, nothing reachable.
Usage: python -I tests/browser-import/make_sources.py <root> [--local-port N]   (N: the « Local » bookmark's port, 8080 by default)
"""
import json, os, sqlite3, struct, sys, zlib

EPOCH_DELTA_MS = 11644473600000


def png(rgb, size=16):
    """A real size x size PNG of one colour (the browser decodes and draws it)."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    rows = b''.join(b'\x00' + bytes(rgb) * size for _ in range(size))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def chromium_favicons(path, icons):
    """Chromium's « Favicons » database: icons = [(page_url, icon_url, png_bytes, width)]."""
    db = sqlite3.connect(path)
    db.execute('CREATE TABLE favicons(id INTEGER PRIMARY KEY, url LONGVARCHAR NOT NULL, icon_type INTEGER DEFAULT 1)')
    db.execute('CREATE TABLE favicon_bitmaps(id INTEGER PRIMARY KEY, icon_id INTEGER NOT NULL, last_updated INTEGER DEFAULT 0, image_data BLOB, width INTEGER DEFAULT 0, height INTEGER DEFAULT 0, last_requested INTEGER DEFAULT 0)')
    db.execute('CREATE TABLE icon_mapping(id INTEGER PRIMARY KEY, page_url LONGVARCHAR NOT NULL, icon_id INTEGER)')
    for i, (page, icon, data, width) in enumerate(icons, 1):
        db.execute('INSERT INTO favicons(id, url, icon_type) VALUES(?,?,1)', (i, icon))
        db.execute('INSERT INTO favicon_bitmaps(icon_id, image_data, width, height) VALUES(?,?,?,?)', (i, data, width, width))
        db.execute('INSERT INTO icon_mapping(page_url, icon_id) VALUES(?,?)', (page, i))
    db.commit(); db.close()


def chrome_time(unix_ms):
    return str((unix_ms + EPOCH_DELTA_MS) * 1000)


def link(name, url, ms=1700000000000):
    return {'type': 'url', 'name': name, 'url': url, 'date_added': chrome_time(ms), 'guid': '00000000-0000-4000-8000-000000000000', 'id': '1'}


def folder(name, children):
    return {'type': 'folder', 'name': name, 'children': children, 'date_added': chrome_time(1690000000000), 'id': '2'}


def main(root, local_port=8080):
    chrome = os.path.join(root, 'Local', 'Google', 'Chrome', 'User Data', 'Default')
    os.makedirs(chrome, exist_ok=True)
    bar = folder('Bookmarks bar', [
        link('Accueil — été ☀', 'https://home.example/'),
        folder('Travail 🛠 日本', [link('Docs', 'https://docs.example/a'), link('Docs (copie)', 'https://docs.example/a'), link('JS', 'javascript:void(0)')]),
        folder('Perso', [link('Docs', 'https://docs.example/a')]),
        link('Local', 'http://localhost:%d/page' % local_port),
    ])
    other = folder('Other bookmarks', [link('Trois', 'https://three.example/', 1600000000000)])
    doc = {'checksum': '0', 'roots': {'bookmark_bar': bar, 'other': other, 'synced': folder('Mobile bookmarks', [])}, 'version': 1}
    with open(os.path.join(chrome, 'Bookmarks'), 'w', encoding='utf-8') as f:
        json.dump(doc, f, ensure_ascii=False, indent=3)
    db = sqlite3.connect(os.path.join(chrome, 'History'))
    db.execute('CREATE TABLE urls(id INTEGER PRIMARY KEY, url LONGVARCHAR, title LONGVARCHAR, visit_count INTEGER DEFAULT 0 NOT NULL, typed_count INTEGER DEFAULT 0 NOT NULL, last_visit_time INTEGER NOT NULL, hidden INTEGER DEFAULT 0 NOT NULL)')
    rows = [
        (1, 'https://home.example/', 'Accueil — été', 5, 1763000000000, 0),
        (2, 'https://news.example/today', 'Nouvelles 漢字', 2, 1763100000000, 0),
        (3, 'https://hidden.example/', 'caché', 1, 1763200000000, 1),
        (4, 'chrome://settings/', 'Paramètres', 3, 1763300000000, 0),
        (5, 'https://www.google.com/search?q=example', 'search', 1, 1763400000000, 0),
    ]
    for i, url, title, visits, ms, hidden in rows:
        db.execute('INSERT INTO urls(id, url, title, visit_count, last_visit_time, hidden) VALUES(?,?,?,?,?,?)', (i, url, title, visits, int(chrome_time(ms)), hidden))
    db.commit(); db.close()
    # The bookmarks' icons as Chrome keeps them (home and docs have one; three.example has none: the browser fetches it).
    chromium_favicons(os.path.join(chrome, 'Favicons'), [
        ('https://home.example/', 'https://home.example/favicon.ico', png((0xE5, 0x48, 0x2B)), 16),
        ('https://docs.example/a', 'https://docs.example/favicon.ico', png((0x1A, 0x73, 0xE8), 32), 32),
    ])

    edge = os.path.join(root, 'Local', 'Microsoft', 'Edge', 'User Data', 'Profile 1')
    os.makedirs(edge, exist_ok=True)
    with open(os.path.join(edge, 'Bookmarks'), 'w', encoding='utf-8') as f:
        json.dump({'roots': {'bookmark_bar': folder('Favorites bar', [link('Edge one', 'https://edge.example/')])}, 'version': 1}, f)

    ff_root = os.path.join(root, 'Roaming', 'Mozilla', 'Firefox')
    profile = os.path.join(ff_root, 'Profiles', 'e2e.default-release')
    os.makedirs(profile, exist_ok=True)
    with open(os.path.join(ff_root, 'profiles.ini'), 'w', encoding='utf-8') as f:
        f.write('[Profile0]\nName=default-release\nIsRelative=1\nPath=Profiles/e2e.default-release\nDefault=1\n')
    db = sqlite3.connect(os.path.join(profile, 'places.sqlite'))
    db.execute('PRAGMA journal_mode=WAL')
    db.execute('CREATE TABLE moz_places(id INTEGER PRIMARY KEY, url LONGVARCHAR, title LONGVARCHAR, visit_count INTEGER DEFAULT 0, hidden INTEGER DEFAULT 0 NOT NULL, last_visit_date INTEGER)')
    db.execute('CREATE TABLE moz_bookmarks(id INTEGER PRIMARY KEY, type INTEGER, fk INTEGER DEFAULT NULL, parent INTEGER, position INTEGER, title LONGVARCHAR, dateAdded INTEGER, guid TEXT)')
    db.execute("INSERT INTO moz_places VALUES(1, 'https://fox.example/', 'Renard', 3, 0, 1763000000000000)")
    db.executemany('INSERT INTO moz_bookmarks VALUES(?,?,?,?,?,?,?,?)', [
        (1, 2, None, 0, 0, '', 1, 'root________'), (2, 2, None, 1, 0, 'menu', 1, 'menu________'), (3, 2, None, 1, 1, 'toolbar', 1, 'toolbar_____'),
        (10, 1, 1, 3, 0, 'Renard', 1763000000000000, 'b1')])
    db.commit(); db.close()   # clean close: no -wal left, the database stays in WAL mode

    html = ('<!DOCTYPE NETSCAPE-Bookmark-file-1>\n<META HTTP-EQUIV="Content-Type" CONTENT="text/html; charset=UTF-8">\n'
            '<TITLE>Bookmarks</TITLE>\n<H1>Bookmarks</H1>\n<DL><p>\n'
            '    <DT><H3 ADD_DATE="1700000000">Exporté &amp; rangé</H3>\n    <DL><p>\n'
            '        <DT><A HREF="https://html.example/one" ADD_DATE="1700000000">Un — ①</A>\n'
            '        <DT><A HREF="javascript:alert(1)">piège</A>\n'
            '    </DL><p>\n'
            '    <script>document.write("<DT><A HREF=\\"https://script.example/\\">no</A>")</script>\n'
            '    <DT><A HREF="https://html.example/two">Deux</A>\n</DL><p>\n')
    with open(os.path.join(root, 'export é#.html'), 'w', encoding='utf-8') as f:
        f.write(html)
    # Installed browsers as Windows lists them (src/browser-catalog.h reads this file instead of the registry under
    # --import-sources). Nothing here is run: only names, icon references and command paths. Comet is installed with no
    # profile data (a disabled row), Opera is not supported (a disabled row; its UNC icon reference is refused unread),
    # Chrome's logo is read as data from the system's explorer.exe (a stand-in local executable).
    windows = os.environ.get('SystemRoot', r'C:\Windows')
    with open(os.path.join(root, 'startmenuinternet.txt'), 'w', encoding='utf-8') as f:
        rows = [('Google Chrome', 'Google Chrome', r'"C:\Program Files\Google\Chrome\Application\chrome.exe" --single-argument %1', windows + r'\explorer.exe,0'),
                ('Microsoft Edge', 'Microsoft Edge', r'"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" --single-argument %1', ''),
                ('FIREFOX.EXE', 'Mozilla Firefox', r'"C:\Program Files\Mozilla Firefox\firefox.exe" -osint -url "%1"', ''),
                ('Comet', 'Comet', r'"C:\Users\e2e\AppData\Local\Perplexity\Comet\Application\comet.exe" --single-argument %1', ''),
                ('OperaStable', 'Opera Stable', r'"C:\Program Files\Opera\opera.exe" %1', r'\\e2e-server\icons\opera.ico')]
        f.write(''.join('|'.join(r) + '\n' for r in rows))




def big(root, links=180000):
    """A large Chromium profile (bookmarks only, ~30 MB): its read lasts long enough to switch profile meanwhile."""
    chrome = os.path.join(root, 'Local', 'Google', 'Chrome', 'User Data', 'Default')
    os.makedirs(chrome, exist_ok=True)
    folders = [folder('Lot %d' % k, [link('Page %d' % i, 'https://big.example/%d' % i) for i in range(k * 1000, min(links, (k + 1) * 1000))]) for k in range((links + 999) // 1000)]
    with open(os.path.join(chrome, 'Bookmarks'), 'w', encoding='utf-8') as f:
        json.dump({'roots': {'bookmark_bar': folder('Bookmarks bar', folders)}, 'version': 1}, f, ensure_ascii=False)


if __name__ == '__main__':
    if len(sys.argv) > 2 and sys.argv[2] == '--big':
        big(sys.argv[1])
    elif len(sys.argv) > 3 and sys.argv[2] == '--local-port':   # the E2E's own fixture server (the « Local » bookmark's icon)
        main(sys.argv[1], int(sys.argv[3]))
    else:
        main(sys.argv[1])
