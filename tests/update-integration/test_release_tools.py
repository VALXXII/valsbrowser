"""Deterministic tests for tools/release (no network, no GitHub mutation).

Covers: build identity (monotonic, shallow refusal, input validation), publish
decision guards (never regress the feed), payload staging allowlist with canary
runtime data that must never be packaged nor modified, manifest parsing, the public
first-run ZIP (exact staged entries and bytes, checksums, release-info), the
publisher end to end against an in-memory fake GitHub (positive and negative cases),
and the workflow file's security-relevant properties; the « Nouveautés » notes of the release
(release-notes/notes.json: the committed file is valid, the rules of release-notes/README.md, the history per version
read from git, the cumulative selection, the generated header).
"""
from __future__ import annotations
import contextlib, hashlib, io, json, os, re, shutil, subprocess, sys, tempfile, unittest, zipfile
from unittest import mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/release'))
import version as V  # noqa: E402
import publish as P  # noqa: E402
import package as K  # noqa: E402
import build as B  # noqa: E402
import release_notes as R  # noqa: E402

SHA = 'a' * 40


class Identity(unittest.TestCase):
    def test_version_shape(self):
        info = V.identity(42, SHA, 'release')
        self.assertEqual(info['version'], '0.3.42')
        self.assertEqual(info['tag'], 'v0.3.42')
        self.assertEqual(info['build'], 42)

    def test_rejects_bad_inputs(self):
        for args in ((0, SHA, 'dev'), (70000, SHA, 'dev'), (5, 'xyz', 'dev'), (5, SHA, 'beta')):
            with self.assertRaises(SystemExit):
                V.identity(*args)

    def test_monotonic_in_merge_order_and_refuses_shallow(self):
        git = shutil.which('git')
        if not git:
            self.skipTest('git unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp) / 'r'
            def g(*a, cwd=repo):
                return subprocess.run(['git', '-c', 'user.name=t', '-c', 'user.email=t@example.invalid',
                                       '-c', 'commit.gpgsign=false', *a], cwd=cwd, check=True,
                                      capture_output=True, text=True).stdout.strip()
            repo.mkdir()
            g('init', '-q', '-b', 'main')
            g('commit', '-q', '--allow-empty', '-m', 'one')
            counts = [int(g('rev-list', '--count', 'HEAD'))]
            g('checkout', '-q', '-b', 'feature')
            g('commit', '-q', '--allow-empty', '-m', 'f1')
            g('commit', '-q', '--allow-empty', '-m', 'f2')
            g('checkout', '-q', 'main')
            g('commit', '-q', '--allow-empty', '-m', 'two')
            counts.append(int(g('rev-list', '--count', 'HEAD')))
            # merge with origin/main as SECOND parent (the style that breaks --first-parent)
            g('checkout', '-q', 'feature')
            g('merge', '-q', '--no-ff', '-m', 'merge main into feature', 'main')
            g('checkout', '-q', 'main')
            g('merge', '-q', '--ff-only', 'feature')
            counts.append(int(g('rev-list', '--count', 'HEAD')))
            g('commit', '-q', '--allow-empty', '-m', 'three')
            counts.append(int(g('rev-list', '--count', 'HEAD')))
            self.assertEqual(counts, sorted(set(counts)), counts)
            shallow = Path(tmp) / 's'
            g('clone', '-q', '--depth', '1', f'file://{repo.as_posix()}', str(shallow), cwd=tmp)
            self.assertEqual(V.identity(repo=repo)['build'], counts[-1])
            with self.assertRaises(SystemExit):
                V.identity(repo=shallow)


class AppIconPolicy(unittest.TestCase):
    def test_channel_selects_icon(self):
        live, preview = V.app_icon('release'), V.app_icon('dev')
        self.assertEqual((live.name, preview.name), ('browser-live.ico', 'browser-preview.ico'))
        self.assertNotEqual(live.read_bytes(), preview.read_bytes())
        with self.assertRaises(SystemExit):
            V.app_icon('beta')

    def test_icos_are_multi_size_with_alpha(self):
        import struct
        for channel in ('release', 'dev'):
            data = V.app_icon(channel).read_bytes()
            count = struct.unpack_from('<HHH', data)[2]
            sizes = []
            for i in range(count):
                w, h, _, _, planes, bpp, size, offset = struct.unpack_from('<BBBBHHII', data, 6 + 16 * i)
                self.assertEqual((planes, bpp), (1, 32))
                self.assertLessEqual(offset + size, len(data))
                sizes.append(w or 256)
            self.assertEqual(sizes, [16, 20, 24, 32, 40, 48, 64, 128, 256])

    def test_build_info_marks_only_release(self):
        with tempfile.TemporaryDirectory() as tmp:
            for channel, marked in (('release', True), ('dev', False)):
                B.write_build_info(Path(tmp) / channel, V.identity(42, SHA, channel), 'o/r')
                text = (Path(tmp) / channel / 'build-info.h').read_text(encoding='ascii')
                self.assertEqual('BROWSER_CHANNEL_RELEASE' in text, marked)

    def test_release_requires_a_commit_on_main(self):
        if not shutil.which('git'):
            self.skipTest('git unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            def g(*a):
                return subprocess.run(['git', '-c', 'user.name=t', '-c', 'user.email=t@example.invalid',
                                       '-c', 'commit.gpgsign=false', *a], cwd=repo, check=True,
                                      capture_output=True, text=True).stdout.strip()
            g('init', '-q', '-b', 'main')
            g('commit', '-q', '--allow-empty', '-m', 'one')
            on_main = g('rev-parse', 'HEAD')
            g('checkout', '-q', '-b', 'worktree-feature')
            g('commit', '-q', '--allow-empty', '-m', 'branch only')
            branch = g('rev-parse', 'HEAD')
            self.assertEqual(V.require_on_main(on_main, repo), 'main')
            with self.assertRaises(SystemExit):
                V.require_on_main(branch, repo)


class PublishGuards(unittest.TestCase):
    def test_decide(self):
        self.assertEqual(P.decide((0, 3, 10), [], False), 'publish')
        self.assertEqual(P.decide((0, 3, 10), [(0, 3, 9)], False), 'publish')
        self.assertEqual(P.decide((0, 3, 10), [(0, 3, 11)], False), 'superseded')
        self.assertEqual(P.decide((0, 3, 10), [(0, 3, 10)], True), 'verify-existing')
        # numeric, not lexicographic
        self.assertEqual(P.decide((0, 3, 100), [(0, 3, 99)], False), 'publish')
        self.assertEqual(P.decide((0, 3, 9), [(0, 3, 10)], False), 'superseded')

    def test_published_versions_ignore_drafts_and_foreign_tags(self):
        items = [{'tag_name': 'v0.3.5'}, {'tag_name': 'v0.3.9', 'draft': True},
                 {'tag_name': 'nightly'}, {'tag_name': 'v0.03.1'}, {'tag_name': 'v1.0.0'}]
        self.assertEqual(sorted(P.published_versions(items)), [(0, 3, 5), (1, 0, 0)])

    def test_parse_tag(self):
        self.assertIsNone(P.parse_tag('0.3.1'))
        self.assertIsNone(P.parse_tag('v0.3.01'))
        self.assertEqual(P.parse_tag('v0.3.1'), (0, 3, 1))


def fake_setup_stub(identity: str = f'0.3.7 {SHA} release', subsystem: int = 2, manifest: bool = True,
                    extra: bytes = b'', marker: bytes = b'') -> bytes:
    """Minimal PE32+ x64 image with one section holding what package.check_setup_stub reads (not runnable)."""
    data = bytearray(0x400)
    data[:2] = b'MZ'
    data[0x3C:0x40] = (0x80).to_bytes(4, 'little')
    data[0x80:0x84] = b'PE\0\0'
    data[0x84:0x86] = (0x8664).to_bytes(2, 'little')
    data[0x86:0x88] = (1).to_bytes(2, 'little')          # one section
    data[0x94:0x96] = (0xF0).to_bytes(2, 'little')       # SizeOfOptionalHeader
    opt = 0x80 + 24
    data[opt:opt + 2] = (0x20B).to_bytes(2, 'little')
    data[opt + 60:opt + 64] = (0x200).to_bytes(4, 'little')  # SizeOfHeaders
    data[opt + 68:opt + 70] = subsystem.to_bytes(2, 'little')
    data[opt + 108:opt + 112] = (16).to_bytes(4, 'little')  # NumberOfRvaAndSizes
    sec = opt + 0xF0
    data[sec:sec + 5] = b'.data'
    data[sec + 16:sec + 20] = (0x200).to_bytes(4, 'little')  # SizeOfRawData
    data[sec + 20:sec + 24] = (0x200).to_bytes(4, 'little')  # PointerToRawData
    body = b'browser-setup-identity ' + identity.encode() + b';'
    if manifest:
        body += b' <requestedExecutionLevel level="asInvoker" uiAccess="false"/>'
    body += marker
    data[0x200:0x200 + len(body)] = body
    return bytes(data) + extra


def fake_sign(data: bytes, table: bytes = b'CERTIFICATE-TABLE' + b'\0' * 7) -> bytes:
    """What a signing tool does to a PE file, without any cryptography: zeros up to an 8-byte boundary, a certificate
    table at the end, the header's certificate directory pointing at it, and a new CheckSum."""
    pe = int.from_bytes(data[0x3C:0x40], 'little')
    opt = pe + 24
    body = bytearray(data + b'\0' * ((-len(data)) % 8))
    body[opt + 144:opt + 148] = len(body).to_bytes(4, 'little')
    body[opt + 148:opt + 152] = len(table).to_bytes(4, 'little')
    body[opt + 64:opt + 68] = (0x5167).to_bytes(4, 'little')
    return bytes(body) + table


def make_app(root: Path) -> Path:
    app = root / 'app'
    (app / 'assets/fonts').mkdir(parents=True)
    for name in ('browser.exe', 'update-helper.exe', 'WebView2Loader.dll', 'ThorVG-LICENSE.txt',
                 'WebView2-NOTICE.txt'):
        (app / name).write_bytes(name.encode())
    (app / 'assets/fonts/a.ttf').write_bytes(b'font')
    (app / 'version.json').write_text(json.dumps({'version': '0.3.7', 'arch': 'x64', 'commit': SHA,
                                                  'channel': 'release'}))
    # Setup stub as tools/release/build.py places it: <app>-setup/browser-setup.exe (never in the payload).
    (root / 'app-setup').mkdir()
    (root / 'app-setup/browser-setup.exe').write_bytes(fake_setup_stub())
    return app


def canary(path: Path) -> str:
    h = hashlib.sha256()
    for f in sorted(p for p in path.rglob('*') if p.is_file()):
        h.update(f.relative_to(path).as_posix().encode() + b'\0' + f.read_bytes())
    return h.hexdigest()


class Staging(unittest.TestCase):
    def test_runtime_data_never_packaged_or_touched(self):
        with tempfile.TemporaryDirectory() as tmp:
            app = make_app(Path(tmp))
            (app / 'profile/EBWebView').mkdir(parents=True)
            (app / 'profile/EBWebView/Cookies').write_bytes(b'canary-cookie')
            (app / 'session').write_text('browser-session-1\n0\n1\n"https://canary.example"\n')
            (app / 'prototype.log').write_text('log')
            (app / 'ui-language').write_text('browser-ui-language 1\nlanguage=en\n')   # interface language preference
            (app / 'ui-language.tmp4242').write_text('browser-ui-language 1\n')         # an interrupted atomic write
            (app / 'bookmark-imports').write_text('browser-bookmark-imports 1\nchromium:Chrome/Default\t5\t1\n')   # import folder identities
            (app / 'bookmark-imports.tmp4242').write_text('browser-bookmark-imports 1\n')                              # an interrupted atomic write
            (app / 'release-notes').write_text('browser-release-notes 1\nseen=canary-notes\n')   # « Nouveautés » read state
            (app / 'release-notes.tmp4242').write_text('browser-release-notes 1\n')              # an interrupted atomic write
            before = canary(app)
            files = K.stage(app, Path(tmp) / 'stage')
            self.assertEqual(canary(app), before)
            self.assertFalse(any('profile' in f or f in ('session', 'prototype.log') or f.startswith(('ui-language', 'bookmark-imports', 'release-notes')) for f in files))
            self.assertIn('assets/fonts/a.ttf', files)
            self.assertFalse((Path(tmp) / 'stage/profile').exists())

    def test_unexpected_entries_refused(self):
        for bad in ('notes.txt', 'browser-res.o', 'build-obj/', 'tests/'):
            with self.subTest(bad=bad), tempfile.TemporaryDirectory() as tmp:
                app = make_app(Path(tmp))
                if bad.endswith('/'):
                    (app / bad).mkdir()
                else:
                    (app / bad).write_text('x')
                with self.assertRaises(SystemExit):
                    K.stage(app, Path(tmp) / 'stage')

    def test_required_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            app = make_app(Path(tmp))
            (app / 'update-helper.exe').unlink()
            with self.assertRaises(SystemExit):
                K.stage(app, Path(tmp) / 'stage')


class Manifest(unittest.TestCase):
    def test_parse(self):
        with tempfile.TemporaryDirectory() as tmp:
            m = Path(tmp) / 'm.txt'
            m.write_bytes(b'browser-update 1\nversion 0.3.7\narch x64\npackage p.lsrupd\nsize 3\nsha256 ' + b'0' * 64 + b'\n')
            self.assertEqual(K.read_manifest(m)['version'], '0.3.7')
            m.write_bytes(m.read_bytes().replace(b'\n', b'\r\n'))
            with self.assertRaises(SystemExit):
                K.read_manifest(m)
            m.write_bytes(b'browser-update 1\nversion 0.3.7\n')
            with self.assertRaises(SystemExit):
                K.read_manifest(m)


def package_dist(root: Path, app: Path) -> Path:
    """Run package.py end to end (real update-package packer) on a synthetic app."""
    out = root / 'dist'
    with mock.patch.object(sys, 'argv', ['package.py', '--app', str(app), '--out', str(out)]), \
            contextlib.redirect_stdout(io.StringIO()):
        K.main()
    return out


def app_files(app: Path) -> dict:
    keep = {'browser.exe', 'update-helper.exe', 'WebView2Loader.dll', 'version.json',
            'ThorVG-LICENSE.txt', 'WebView2-NOTICE.txt'}
    files = {f.relative_to(app).as_posix(): f.read_bytes() for f in app.rglob('*') if f.is_file()}
    return {k: v for k, v in files.items() if k in keep or k.startswith('assets/')}


class PublicZip(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.app = make_app(self.root)
        # Runtime data of a portable run beside the exe: never packaged, never modified.
        (self.app / 'profile/EBWebView').mkdir(parents=True)
        (self.app / 'profile/EBWebView/Cookies').write_bytes(b'canary-cookie')
        (self.app / 'annotations').mkdir()
        (self.app / 'annotations/store.json').write_text('{"canary":1}')
        (self.app / 'session').write_text('canary-session')
        (self.app / 'prototype.log').write_text('canary-log')

    def tearDown(self):
        self.tmp.cleanup()

    def test_zip_is_exactly_the_staged_payload(self):
        before = canary(self.app)
        dist = package_dist(self.root, self.app)
        self.assertEqual(canary(self.app), before, 'packaging modified the app directory')
        archive = dist / 'browser-0.3.7-win-x64.zip'
        expected = app_files(self.app)
        with zipfile.ZipFile(archive) as z:
            self.assertIsNone(z.testzip())
            names = z.namelist()
            self.assertEqual(sorted(names), sorted(f'browser-0.3.7-win-x64/{k}' for k in expected))
            for name in names:
                self.assertFalse(name.startswith('/') or '..' in name.split('/') or '\\' in name, name)
                self.assertNotRegex(name.lower(), r'profile|session|annotations/|\.log$|cookies|mcp')
                self.assertEqual(z.read(name), expected[name.split('/', 1)[1]], name)
                info = z.getinfo(name)
                self.assertEqual(info.date_time, (1980, 1, 1, 0, 0, 0))
                self.assertFalse(info.is_dir())
        # The ZIP carries the same version identity as the update package.
        with zipfile.ZipFile(archive) as z:
            self.assertEqual(json.loads(z.read('browser-0.3.7-win-x64/version.json'))['version'], '0.3.7')
        self.assertEqual(sorted(p.name for p in dist.iterdir()), sorted([
            'browser-0.3.7-win-x64.lsrupd', 'update-manifest-win-x64.txt', 'browser-0.3.7-win-x64.zip',
            'browser-0.3.7-win-x64-setup.exe', 'SHA256SUMS.txt', 'release-info.json']))

    def test_checksums_and_release_info_cover_zip(self):
        dist = package_dist(self.root, self.app)
        archive = dist / 'browser-0.3.7-win-x64.zip'
        sums = (dist / 'SHA256SUMS.txt').read_bytes().decode('ascii')
        lines = dict(reversed(l.split('  ')) for l in sums.splitlines())
        self.assertEqual(set(lines), {'browser-0.3.7-win-x64.lsrupd', 'update-manifest-win-x64.txt', archive.name,
                                      'browser-0.3.7-win-x64-setup.exe'})
        for name, digest in lines.items():
            self.assertEqual(digest, hashlib.sha256((dist / name).read_bytes()).hexdigest(), name)
        info = json.loads((dist / 'release-info.json').read_text(encoding='utf-8'))
        self.assertEqual((info['zip'], info['zipSize'], info['zipSha256']),
                         (archive.name, archive.stat().st_size, lines[archive.name]))
        self.assertEqual(info['files'], len(app_files(self.app)))

    def test_zip_is_reproducible(self):
        a = (package_dist(self.root, self.app) / 'browser-0.3.7-win-x64.zip').read_bytes()
        b = (package_dist(self.root, self.app) / 'browser-0.3.7-win-x64.zip').read_bytes()
        self.assertEqual(hashlib.sha256(a).digest(), hashlib.sha256(b).digest())

    def test_private_or_unknown_entries_fail_packaging(self):
        for bad in ('browser-annotations-mcp.exe', '.mcp.json', 'settings', 'profiles/', '.git/', 'src/'):
            with self.subTest(bad=bad), tempfile.TemporaryDirectory() as tmp:
                app = make_app(Path(tmp))
                (app / bad).mkdir() if bad.endswith('/') else (app / bad).write_text('secret')
                with self.assertRaises(SystemExit), contextlib.redirect_stdout(io.StringIO()):
                    package_dist(Path(tmp), app)
                self.assertFalse((Path(tmp) / 'dist/browser-0.3.7-win-x64.zip').exists())

    def test_verify_zip_rejects_extra_missing_or_altered_entries(self):
        stage = self.root / 'stage'
        files = K.stage(self.app, stage)
        top = 'browser-0.3.7-win-x64'
        good = self.root / 'good.zip'
        K.write_zip(stage, files, good, top)
        K.verify_zip(good, stage, files, top)
        with self.assertRaises(SystemExit):
            K.write_zip(stage, files, good, top)  # never overwrites
        def variant(name, mutate):
            path = self.root / name
            with zipfile.ZipFile(good) as src, zipfile.ZipFile(path, 'w') as dst:
                for item in src.infolist():
                    data = mutate(item.filename, src.read(item))
                    if data is not None:
                        dst.writestr(item.filename, data)
                if name == 'extra.zip':
                    dst.writestr(f'{top}/profile/Cookies', b'x')
            return path
        bad = [variant('extra.zip', lambda n, d: d),
               variant('missing.zip', lambda n, d: None if n.endswith('a.ttf') else d),
               variant('altered.zip', lambda n, d: d + b'!' if n.endswith('browser.exe') else d)]
        for path in bad:
            with self.subTest(path=path.name), self.assertRaises(SystemExit):
                K.verify_zip(path, stage, files, top)

    def junction_case(self, where):
        """Unprivileged Windows junction (is_symlink() is False for it): packaging must fail and
        no ZIP may be written, whether the junction is assets itself or nested inside it."""
        try:
            import _winapi
        except ImportError:
            self.skipTest('Windows junctions only')
        private = self.root / 'outside-private'
        private.mkdir()
        (private / 'Cookies').write_bytes(b'FAKE private cookie canary')
        link = self.app / where
        if where == 'assets':
            shutil.rmtree(link)
        _winapi.CreateJunction(str(private), str(link))
        try:
            self.assertFalse(link.is_symlink(), 'fixture must be a junction, not a symlink')
            with self.assertRaises(SystemExit) as cm, contextlib.redirect_stdout(io.StringIO()):
                package_dist(self.root, self.app)
            self.assertIn('reparse', str(cm.exception))
            dist = self.root / 'dist'
            self.assertFalse(dist.exists() and any(dist.glob('*.zip')), 'no ZIP may be emitted')
            staged = self.root / 'dist-payload'
            self.assertFalse(any(f.read_bytes() == b'FAKE private cookie canary'
                                 for f in staged.rglob('*') if f.is_file()) if staged.exists() else False)
        finally:
            os.rmdir(link)  # removes the junction only, never the target's content
        self.assertEqual((private / 'Cookies').read_bytes(), b'FAKE private cookie canary')

    def test_nested_junction_in_assets_refused(self):
        self.junction_case('assets/joined')

    def test_assets_junction_refused(self):
        self.junction_case('assets')

    def test_symlink_in_payload_refused(self):
        try:
            os.symlink(self.app / 'browser.exe', self.app / 'assets/link.ttf')
        except (OSError, NotImplementedError):
            self.skipTest('symlinks need privileges here')
        with self.assertRaises(SystemExit):
            K.stage(self.app, self.root / 'stage')


REPO = 'VALXXII/browser-releases'


class FakeGitHub:
    """In-memory releases repository: gh() for the publisher, fetch() for anonymous URLs."""
    def __init__(self, existing=None, corrupt_upload=None, corrupt_public=None):
        self.releases = list(existing or [])
        self.calls = []
        self.corrupt_upload = corrupt_upload  # asset name whose server digest is wrong
        self.corrupt_public = corrupt_public  # asset name served with different bytes
        self.blobs = {}
        for r in self.releases:  # an existing release serves its files anonymously (its 'files' are not API fields)
            for name, data in r.get('files', {}).items():
                self.blobs[(r['tag_name'], name)] = data

    def gh(self, *args, check=True, capture=True):
        self.calls.append(args)
        if args[0] == 'api':
            out = json.dumps([{k: v for k, v in r.items() if k != 'files'} for r in self.releases])
            return subprocess.CompletedProcess(args, 0, out, '')
        verb, tag = args[1], args[2]
        if verb == 'create':
            files = [Path(x) for x in args[args.index('--notes') + 2:]]
            assets = []
            for f in files:
                digest = hashlib.sha256(f.read_bytes()).hexdigest()
                if f.name == self.corrupt_upload:
                    digest = '0' * 64
                assets.append({'name': f.name, 'digest': 'sha256:' + digest})
                self.blobs[(tag, f.name)] = f.read_bytes()
            self.notes = args[args.index('--notes') + 1]
            self.releases.append({'tag_name': tag, 'draft': True, 'assets': assets})
        elif verb == 'edit':
            for r in self.releases:
                if r['tag_name'] == tag:
                    if '--draft=false' in args:
                        r['draft'] = False
                    if '--latest' in args:
                        for o in self.releases:
                            o['latest'] = o is r
        elif verb == 'delete':
            self.releases = [r for r in self.releases if r['tag_name'] != tag]
        return subprocess.CompletedProcess(args, 0, '', '')

    def fetch(self, url, limit=4096):
        prefix = f'https://github.com/{REPO}/releases/'
        assert url.startswith(prefix), url
        rest = url[len(prefix):].split('/')
        if rest[0] == 'latest':
            tag = next(r['tag_name'] for r in self.releases if r.get('latest') and not r.get('draft'))
            name = rest[2]
        else:
            tag, name = rest[1], rest[2]
            assert not next(r for r in self.releases if r['tag_name'] == tag).get('draft')
        data = self.blobs[(tag, name)]
        if name == self.corrupt_public:
            data = data + b'tampered'
        return data[:limit]

    def mutations(self):
        return [c for c in self.calls if c[0] == 'release']


class Publisher(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        root = Path(cls.tmp.name)
        app = make_app(root)
        info = json.loads((app / 'version.json').read_text())
        info.update({'commit': SHA, 'channel': 'release'})
        (app / 'version.json').write_text(json.dumps(info))
        cls.dist = package_dist(root, app)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.d = Path(self.work.name) / 'dist'
        shutil.copytree(self.dist, self.d)

    def tearDown(self):
        self.work.cleanup()

    def run_publish(self, fake, *extra):
        out = io.StringIO()
        with mock.patch.object(P, 'gh', fake.gh), mock.patch.object(P, 'fetch', fake.fetch), \
                mock.patch.object(P.time, 'sleep', lambda s: None), \
                mock.patch.dict(os.environ, {'GH_TOKEN': 'test-token-not-real'}), contextlib.redirect_stdout(out):
            code = P.main(['--repo', REPO, '--dist', str(self.d), '--source-commit', SHA, *extra])
        return code, out.getvalue()

    def existing_release(self, with_zip=True, zip_bytes=None, with_sums=False, with_setup=True):
        assets, files = [], {}
        for name in ('browser-0.3.7-win-x64.lsrupd', 'update-manifest-win-x64.txt', 'browser-0.3.7-win-x64.zip',
                     *(('browser-0.3.7-win-x64-setup.exe',) if with_setup else ()),
                     *(('SHA256SUMS.txt',) if with_sums else ())):
            if name.endswith('.zip') and not with_zip:
                continue
            data = zip_bytes if (name.endswith('.zip') and zip_bytes is not None) else (self.d / name).read_bytes()
            assets.append({'name': name, 'digest': 'sha256:' + hashlib.sha256(data).hexdigest()})
            files[name] = data
        return {'tag_name': 'v0.3.7', 'draft': False, 'latest': True, 'assets': assets, 'files': files}

    def test_publishes_zip_with_verified_digest_and_instructions(self):
        fake = FakeGitHub(existing=[{'tag_name': 'v0.3.6', 'draft': False, 'latest': True, 'assets': []}])
        code, out = self.run_publish(fake)
        self.assertEqual(code, 0)
        create = next(c for c in fake.calls if c[:2] == ('release', 'create'))
        self.assertIn(str(self.d / 'browser-0.3.7-win-x64.zip'), create)
        self.assertIn('--draft', create)
        url = f'https://github.com/{REPO}/releases/download/v0.3.7/browser-0.3.7-win-x64.zip'
        self.assertIn(url, fake.notes)
        self.assertIn(f'https://github.com/{REPO}/releases/latest', fake.notes)
        self.assertIn('browser.exe', fake.notes)
        self.assertIn('Extraire tout', fake.notes)
        self.assertIn(json.loads((self.d / 'release-info.json').read_text())['zipSha256'], fake.notes)
        self.assertIn(f'Portable ZIP: {url}', out)
        published = next(r for r in fake.releases if r['tag_name'] == 'v0.3.7')
        self.assertFalse(published['draft'])
        self.assertTrue(published['latest'])

    def test_missing_zip_is_never_success(self):
        cases = {
            'no zip in release-info': lambda: self.edit_info(lambda i: i.pop('zip')),
            'zip file absent': lambda: (self.d / 'browser-0.3.7-win-x64.zip').unlink(),
            'zip renamed to another version': lambda: self.edit_info(lambda i: i.update(zip='browser-0.3.6-win-x64.zip')),
        }
        for label, mutate in cases.items():
            with self.subTest(label):
                self.tearDown(); self.setUp()
                mutate()
                fake = FakeGitHub()
                with self.assertRaises(SystemExit):
                    self.run_publish(fake)
                self.assertEqual(fake.mutations(), [], 'nothing may be uploaded')

    def edit_info(self, change):
        path = self.d / 'release-info.json'
        info = json.loads(path.read_text(encoding='utf-8'))
        change(info)
        path.write_text(json.dumps(info), encoding='utf-8')

    def test_tampered_zip_or_checksums_refused_before_upload(self):
        cases = {
            'zip bytes changed': lambda: (self.d / 'browser-0.3.7-win-x64.zip').write_bytes(b'PK not ours'),
            'zip size mismatch': lambda: self.edit_info(lambda i: i.update(zipSize=i['zipSize'] + 1)),
            'SHA256SUMS without zip': lambda: (self.d / 'SHA256SUMS.txt').write_text(
                ''.join(l + '\n' for l in (self.d / 'SHA256SUMS.txt').read_text().splitlines() if '.zip' not in l),
                newline='\n'),
            'SHA256SUMS missing': lambda: (self.d / 'SHA256SUMS.txt').unlink(),
            'package changed': lambda: (self.d / 'browser-0.3.7-win-x64.lsrupd').write_bytes(b'x'),
            'dev channel': lambda: self.edit_info(lambda i: i.update(channel='dev')),
            'other commit': lambda: self.edit_info(lambda i: i.update(commit='b' * 40)),
        }
        for label, mutate in cases.items():
            with self.subTest(label):
                self.tearDown(); self.setUp()
                mutate()
                fake = FakeGitHub()
                with self.assertRaises(SystemExit):
                    self.run_publish(fake)
                self.assertEqual(fake.mutations(), [])

    def test_server_zip_digest_mismatch_never_published(self):
        fake = FakeGitHub(corrupt_upload='browser-0.3.7-win-x64.zip')
        with self.assertRaises(SystemExit):
            self.run_publish(fake)
        self.assertNotIn('--draft=false', [a for c in fake.calls for a in c])
        self.assertTrue(all(r['draft'] for r in fake.releases))

    def test_public_zip_mismatch_after_publish_fails(self):
        fake = FakeGitHub(corrupt_public='browser-0.3.7-win-x64.zip')
        with self.assertRaises(SystemExit) as cm:
            self.run_publish(fake)
        self.assertIn('ZIP', str(cm.exception))

    def test_existing_release_needs_identical_zip(self):
        fake = FakeGitHub(existing=[self.existing_release(with_sums=True)])
        fake.blobs[('v0.3.7', 'update-manifest-win-x64.txt')] = (self.d / 'update-manifest-win-x64.txt').read_bytes()
        code, _ = self.run_publish(fake)
        self.assertEqual(code, 0)
        self.assertNotIn(('release', 'create'), [c[:2] for c in fake.calls])
        for existing in (self.existing_release(with_zip=False), self.existing_release(zip_bytes=b'other')):
            with self.subTest(assets=[a['name'] for a in existing['assets']]):
                fake = FakeGitHub(existing=[existing])
                fake.blobs[('v0.3.7', 'update-manifest-win-x64.txt')] = (self.d / 'update-manifest-win-x64.txt').read_bytes()
                with self.assertRaises(SystemExit):
                    self.run_publish(fake)
                self.assertEqual(fake.mutations(), [], 'an existing release is never edited or extended')

    def test_existing_release_without_sums_refused(self):
        existing = self.existing_release()
        fake = FakeGitHub(existing=[existing])
        fake.blobs[('v0.3.7', 'update-manifest-win-x64.txt')] = (self.d / 'update-manifest-win-x64.txt').read_bytes()
        with self.assertRaises(SystemExit):
            self.run_publish(fake)
        self.assertEqual(fake.mutations(), [])
        sums = (self.d / 'SHA256SUMS.txt').read_bytes()
        for data in (sums, b'different sums'):
            with self.subTest(sums=data[:14]):
                existing = self.existing_release()
                existing['assets'].append({'name': 'SHA256SUMS.txt', 'digest': 'sha256:' + hashlib.sha256(data).hexdigest()})
                fake = FakeGitHub(existing=[existing])
                fake.blobs[('v0.3.7', 'update-manifest-win-x64.txt')] = (self.d / 'update-manifest-win-x64.txt').read_bytes()
                if data == sums:
                    self.assertEqual(self.run_publish(fake)[0], 0)
                else:
                    with self.assertRaises(SystemExit):
                        self.run_publish(fake)
                    self.assertEqual(fake.mutations(), [])

    def test_manifest_must_describe_this_package(self):
        """A self-consistent artifact (SHA256SUMS regenerated) whose manifest names another
        version, arch, package, size or hash is refused before any upload."""
        good = (self.d / 'update-manifest-win-x64.txt').read_bytes()
        changes = {'version': (b'version 0.3.7', b'version 0.3.6'), 'arch': (b'arch x64', b'arch arm64'),
                   'package': (b'package browser-0.3.7', b'package browser-0.3.6'),
                   'size': (b'\nsize ', b'\nsize 1'), 'sha256': (b'sha256 ', b'sha256 0'),
                   'CRLF': (b'\n', b'\r\n')}
        for label, (old, new) in changes.items():
            with self.subTest(label):
                self.tearDown(); self.setUp()
                manifest = good.replace(old, new)
                self.assertNotEqual(manifest, good)
                (self.d / 'update-manifest-win-x64.txt').write_bytes(manifest)
                files = ['browser-0.3.7-win-x64.lsrupd', 'update-manifest-win-x64.txt', 'browser-0.3.7-win-x64.zip']
                (self.d / 'SHA256SUMS.txt').write_bytes(''.join(
                    f'{hashlib.sha256((self.d / f).read_bytes()).hexdigest()}  {f}\n' for f in files).encode())
                fake = FakeGitHub()
                with self.assertRaises(SystemExit):
                    self.run_publish(fake)
                self.assertEqual(fake.mutations(), [])

    def test_superseded_and_dry_run_do_not_mutate(self):
        fake = FakeGitHub(existing=[{'tag_name': 'v0.3.8', 'draft': False, 'latest': True, 'assets': []}])
        self.assertEqual(self.run_publish(fake)[0], 0)
        self.assertEqual(fake.mutations(), [])
        fake = FakeGitHub()
        code, out = self.run_publish(fake, '--dry-run')
        self.assertEqual(code, 0)
        self.assertIn('browser-0.3.7-win-x64.zip', out)
        self.assertEqual(fake.mutations(), [])


def fake_pe(machine=0x8664, magic=0x20B, subsystem=3) -> bytes:
    """Minimal PE header: what package.check_console_x64 reads (not runnable)."""
    data = bytearray(0x200)
    data[:2] = b'MZ'
    data[0x3C:0x40] = (0x80).to_bytes(4, 'little')
    data[0x80:0x84] = b'PE\0\0'
    data[0x84:0x86] = machine.to_bytes(2, 'little')
    opt = 0x80 + 24
    data[opt:opt + 2] = magic.to_bytes(2, 'little')
    data[opt + 68:opt + 70] = subsystem.to_bytes(2, 'little')
    return bytes(data) + b'bridge-body'


def lsrupd_entries(path: Path) -> dict:
    """Entries of a .lsrupd v1 (format in tools/update-package/make_update_package.py)."""
    import struct
    data, pos, out = path.read_bytes(), 0, {}
    def take(n):
        nonlocal pos
        pos += n
        return data[pos - n:pos]
    assert take(8) == b'LSRUPD01'
    take(struct.unpack('<H', take(2))[0])
    take(struct.unpack('<H', take(2))[0])
    for _ in range(struct.unpack('<I', take(4))[0]):
        name = take(struct.unpack('<H', take(2))[0]).decode('ascii')
        size = struct.unpack('<Q', take(8))[0]
        take(32)
        out[name] = take(size)
    assert take(8) == b'LSRUEND1' and pos == len(data)
    return out


class ReleaseBridgePayload(unittest.TestCase):
    """Plan B item 1: the annotations MCP bridge ships at exactly assets/bin/<name>."""

    def test_constants_agree(self):
        self.assertEqual(B.RELEASE_BRIDGE, K.RELEASE_BRIDGE)
        self.assertEqual(B.RELEASE_BRIDGE, 'assets/bin/browser-annotations-mcp.exe')

    def test_bridge_location_flag_coherence(self):
        loc = B.bridge_location
        self.assertEqual(loc('release', 'auto', True), 'assets/bin/browser-annotations-mcp.exe')
        self.assertEqual(loc('release', 'on', True), 'assets/bin/browser-annotations-mcp.exe')
        self.assertIsNone(loc('release', 'auto', False))     # --no-annotations measurement baseline
        self.assertIsNone(loc('release', 'off', False))
        self.assertEqual(loc('dev', 'auto', True), 'browser-annotations-mcp.exe')  # dev: sibling
        self.assertIsNone(loc('dev', 'off', True))
        self.assertIsNone(loc('dev', 'auto', False))
        for args in (('release', 'off', True),   # annotation UI without its bridge
                     ('release', 'on', False), ('dev', 'on', False),  # bridge without annotations
                     ('release', 'maybe', True)):
            with self.subTest(args=args), self.assertRaises(SystemExit):
                loc(*args)

    def test_release_annotations_define_only_with_the_bridge(self):
        # src/main.cpp gate: release builds get the annotation mode only with this define.
        d = B.release_annotation_defines
        self.assertEqual(d('release', B.RELEASE_BRIDGE, True), ['-DBROWSER_RELEASE_ANNOTATIONS'])
        self.assertEqual(d('release', None, False), [])
        self.assertEqual(d('release', B.RELEASE_BRIDGE, False), [])
        self.assertEqual(d('release', B.BRIDGE_NAME, True), [])
        self.assertEqual(d('dev', B.BRIDGE_NAME, True), [])
        for channel, mode, ann in (('release', 'auto', True), ('release', 'on', True), ('release', 'auto', False),
                                   ('dev', 'auto', True), ('dev', 'off', True)):
            loc = B.bridge_location(channel, mode, ann)
            self.assertEqual(bool(d(channel, loc, ann)), channel == 'release' and ann, (channel, mode, ann))

    def app_with_bridge(self, root: Path, rel=K.RELEASE_BRIDGE, data=None) -> Path:
        app = make_app(root)
        (app / rel).parent.mkdir(parents=True, exist_ok=True)
        (app / rel).write_bytes(fake_pe() if data is None else data)
        return app

    def test_bridge_packaged_identically_in_zip_and_lsrupd(self):
        with tempfile.TemporaryDirectory() as tmp:
            app = self.app_with_bridge(Path(tmp))
            dist = package_dist(Path(tmp), app)
            pkg = lsrupd_entries(dist / 'browser-0.3.7-win-x64.lsrupd')
            with zipfile.ZipFile(dist / 'browser-0.3.7-win-x64.zip') as z:
                zipped = {n.split('/', 1)[1]: z.read(n) for n in z.namelist()}
            self.assertEqual(pkg, zipped)  # same names, same bytes
            self.assertEqual(pkg[K.RELEASE_BRIDGE], fake_pe())
            self.assertNotIn('browser-annotations-mcp.exe', pkg)  # never at the root

    def test_other_executables_refused(self):
        cases = {
            'root bridge': ('browser-annotations-mcp.exe', fake_pe()),
            'other name in assets/bin': ('assets/bin/other.exe', fake_pe()),
            'bridge elsewhere in assets': ('assets/browser-annotations-mcp.exe', fake_pe()),
            'case variant': ('assets/bin/Browser-Annotations-MCP.exe', fake_pe()),
            'dll in assets': ('assets/bin/helper.dll', fake_pe()),
            'script in assets': ('assets/run.cmd', b'@echo off'),
            'GUI subsystem': (K.RELEASE_BRIDGE, fake_pe(subsystem=2)),
            'x86 image': (K.RELEASE_BRIDGE, fake_pe(machine=0x14C, magic=0x10B)),
            'not a PE': (K.RELEASE_BRIDGE, b'MZ not really'),
        }
        for label, (rel, data) in cases.items():
            with self.subTest(label), tempfile.TemporaryDirectory() as tmp:
                app = make_app(Path(tmp))
                (app / rel).parent.mkdir(parents=True, exist_ok=True)
                (app / rel).write_bytes(data)
                with self.assertRaises(SystemExit), contextlib.redirect_stdout(io.StringIO()):
                    package_dist(Path(tmp), app)
                self.assertFalse((Path(tmp) / 'dist/browser-0.3.7-win-x64.zip').exists())

    def test_existing_clients_accept_the_bridge_path(self):
        """Python mirror of src/update-package.cpp AllowedPayloadPath (unchanged since v0.3.239);
        the C++ validator of v0.3.239 itself is exercised by tests/update-integration/old_validator.py."""
        sys.path.insert(0, str(ROOT / 'tools/update-package'))
        import make_update_package as M
        self.assertTrue(M.allowed(K.RELEASE_BRIDGE) and M.valid_path(K.RELEASE_BRIDGE))
        self.assertFalse(M.allowed('browser-annotations-mcp.exe'))


SETUP = 'browser-0.3.7-win-x64-setup.exe'


class SetupAsset(unittest.TestCase):
    """browser-<v>-win-x64-setup.exe = stub || the .lsrupd byte for byte || trailer (src/setup-payload.h)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.app = make_app(self.root)
        (self.app / 'profile').mkdir()
        (self.app / 'profile/Cookies').write_bytes(b'canary-cookie')

    def tearDown(self):
        self.tmp.cleanup()

    def stub_path(self) -> Path:
        return self.root / 'app-setup/browser-setup.exe'

    def test_setup_embeds_exactly_the_package(self):
        before = canary(self.app)
        dist = package_dist(self.root, self.app)
        self.assertEqual(canary(self.app), before)
        setup, package = (dist / SETUP).read_bytes(), (dist / 'browser-0.3.7-win-x64.lsrupd').read_bytes()
        stub = self.stub_path().read_bytes()
        self.assertEqual(setup[:len(stub)], stub)
        self.assertEqual(setup[len(stub):len(stub) + len(package)], package)
        tail = setup[len(stub) + len(package):]
        zeros, trailer = tail[:-128], tail[-128:]
        # The trailer starts on the first 8-byte boundary of the file after the package (zeros before it): a signature,
        # which must start on such a boundary, then adds nothing between the trailer and its certificate table.
        self.assertEqual(len(setup) % 8, 0)
        self.assertEqual(zeros, b'\0' * ((-(len(stub) + len(package))) % 8))
        self.assertEqual(trailer[96:120], b'\0' * 24, 'no embedded uninstaller in an unsigned release')
        self.assertEqual(trailer[:16], b'BROWSER-SETUP 1\0')
        self.assertEqual(trailer[16:48].rstrip(b'\0'), b'0.3.7')
        self.assertEqual(int.from_bytes(trailer[48:56], 'little'), len(stub))
        self.assertEqual(int.from_bytes(trailer[56:64], 'little'), len(package))
        self.assertEqual(trailer[64:96], hashlib.sha256(package).digest())
        self.assertEqual(trailer[120:], b'BRSETUP!')
        self.assertEqual(K.pe_overlay(setup), (len(stub), len(setup), False))
        # Same payload as the ZIP and the .lsrupd: the setup adds no file and no runtime data.
        entries = lsrupd_entries(dist / 'browser-0.3.7-win-x64.lsrupd')
        self.assertEqual(entries, app_files(self.app))
        self.assertNotIn(b'canary-cookie', setup)
        info = json.loads((dist / 'release-info.json').read_text(encoding='utf-8'))
        self.assertEqual((info['setup'], info['setupSize'], info['setupSha256']),
                         (SETUP, len(setup), hashlib.sha256(setup).hexdigest()))
        sums = (dist / 'SHA256SUMS.txt').read_text(encoding='ascii').splitlines()
        self.assertEqual(sums[-1], f'{hashlib.sha256(setup).hexdigest()}  {SETUP}')
        self.assertIs(info['signed'], False, 'signing is off unless asked for')
        self.assertNotIn('sign', sys.modules, 'the default path never loads the signing module')

    def test_setup_is_reproducible(self):
        a = (package_dist(self.root, self.app) / SETUP).read_bytes()
        b = (package_dist(self.root, self.app) / SETUP).read_bytes()
        self.assertEqual(a, b)

    def test_bad_stub_fails_before_any_output(self):
        cases = {
            'missing': None,
            'other version': fake_setup_stub(f'0.3.6 {SHA} release'),
            'other commit': fake_setup_stub(f'0.3.7 {"b" * 40} release'),
            'dev channel': fake_setup_stub(f'0.3.7 {SHA} dev'),
            'console subsystem': fake_setup_stub(subsystem=3),
            'no asInvoker manifest': fake_setup_stub(manifest=False),
            'stub already has an overlay': fake_setup_stub(extra=b'overlay'),
            'not a PE': b'MZ' + b'\0' * 100,
            'test build (redirection)': fake_setup_stub(marker='BROWSER_SETUP_TEST_ROOT'.encode('utf-16-le')),
            'test build (faults)': fake_setup_stub(marker='BROWSER_SETUP_FAULT'.encode('utf-16-le')),
        }
        for label, data in cases.items():
            with self.subTest(label), tempfile.TemporaryDirectory() as tmp:
                app = make_app(Path(tmp))
                stub = Path(tmp) / 'app-setup/browser-setup.exe'
                stub.unlink()
                if data is not None:
                    stub.write_bytes(data)
                with self.assertRaises(SystemExit), contextlib.redirect_stdout(io.StringIO()):
                    package_dist(Path(tmp), app)
                self.assertFalse((Path(tmp) / 'dist').exists(), 'nothing may be packaged with a bad stub')

    def test_verify_setup_rejects_tampering(self):
        dist = package_dist(self.root, self.app)
        info = json.loads((self.app / 'version.json').read_text())
        good = (dist / SETUP).read_bytes()
        package = dist / 'browser-0.3.7-win-x64.lsrupd'
        n = len(self.stub_path().read_bytes())
        K.verify_setup(dist / SETUP, package, info)

        def flip(data, at):
            b = bytearray(data)
            b[at] ^= 1
            return bytes(b)
        cases = {
            'truncated by one byte': good[:-1],
            'truncated inside the package': good[:n + 10],
            'stub only': good[:n],
            'byte flipped in package': flip(good, n + 20),
            'byte flipped in stub identity': good.replace(b'0.3.7 ' + SHA.encode(), b'0.3.8 ' + SHA.encode()),
            'trailer hash flipped': flip(good, len(good) - 128 + 70),
            'trailer version changed': good[:-128] + good[-128:-112] + b'0.3.8'.ljust(32, b'\0') + good[-80:],
            'trailer offset changed': good[:-80] + (n + 1).to_bytes(8, 'little') + good[-72:],
            'padding not zero': flip(good, len(good) - 10),
            'uninstaller size without the bytes': flip(good, len(good) - 20),
            'uninstaller offset without a size': flip(good, len(good) - 30),
            'eight zeros appended': good + b'\0' * 8,
            'end magic changed': good[:-1] + b'?',
            'bytes appended': good + b'x',
        }
        for label, data in cases.items():
            with self.subTest(label):
                bad = self.root / 'bad.exe'
                bad.write_bytes(data)
                with self.assertRaises(SystemExit):
                    K.verify_setup(bad, package, info)

    def test_trailer_is_aligned_for_every_package_size(self):
        info = {'version': '0.3.7', 'commit': SHA, 'channel': 'release'}
        stub = fake_setup_stub()
        for extra in range(8):
            with self.subTest(extra=extra):
                pkg = self.root / f'p{extra}.lsrupd'
                pkg.write_bytes(b'package' + b'x' * extra)
                data = K.setup_bytes(stub, pkg.read_bytes(), '0.3.7')
                (self.root / 's.exe').write_bytes(data)
                K.verify_setup(self.root / 's.exe', pkg, info, signed=False)
                lay = K.setup_layout(data)
                self.assertEqual((len(data) % 8, lay['trailer'] % 8, lay['signed']), (0, 0, False))
                gap = data[len(stub) + len(pkg.read_bytes()):lay['trailer']]
                self.assertEqual(gap, b'\0' * ((-(len(stub) + 7 + extra)) % 8))
                if gap:   # the zeros are part of the layout: anything else there is refused
                    dirty = bytearray(data)
                    dirty[len(stub) + 7 + extra] = 1
                    (self.root / 's.exe').write_bytes(bytes(dirty))
                    with self.assertRaises(SystemExit):
                        K.verify_setup(self.root / 's.exe', pkg, info)

    def test_a_signature_does_not_move_the_trailer(self):
        """What a signing tool does to a file (pad to 8 bytes, append a certificate table, point the header at it) on
        the new layout and on the old one (trailer right after the package): only the new one is still readable."""
        info = {'version': '0.3.7', 'commit': SHA, 'channel': 'release'}
        stub = fake_setup_stub()
        for extra in range(8):
            with self.subTest(extra=extra):
                pkg = self.root / 'p.lsrupd'
                pkg.write_bytes(b'package' + b'x' * extra)
                new = fake_sign(K.setup_bytes(stub, pkg.read_bytes(), '0.3.7'))
                (self.root / 'new.exe').write_bytes(new)
                K.verify_setup(self.root / 'new.exe', pkg, info, signed=True)
                self.assertEqual(K.setup_layout(new)['end'], K.setup_layout(new)['trailer'] + 128)
                with self.assertRaises(SystemExit):
                    K.verify_setup(self.root / 'new.exe', pkg, info, signed=False)
                body = stub + pkg.read_bytes()
                old = fake_sign(body + K.setup_trailer('0.3.7', len(stub), pkg.read_bytes()))
                (self.root / 'old.exe').write_bytes(old)
                if len(body) % 8:   # 7 sizes out of 8: zeros land between the old trailer and the certificate table
                    with self.assertRaises(SystemExit):
                        K.verify_setup(self.root / 'old.exe', pkg, info)
                else:
                    K.verify_setup(self.root / 'old.exe', pkg, info, signed=True)

    def test_embedded_signed_uninstaller(self):
        info = {'version': '0.3.7', 'commit': SHA, 'channel': 'release'}
        stub = fake_setup_stub()
        pkg = self.root / 'p.lsrupd'
        pkg.write_bytes(b'package-bytes')
        uninstaller = fake_sign(stub)
        self.assertNotEqual(uninstaller[:len(stub)], stub, 'a signature rewrites the certificate directory')
        self.assertEqual(K.unsigned_image(uninstaller[:len(stub)]), K.unsigned_image(stub))
        for outer_signed in (False, True):
            with self.subTest(outer_signed=outer_signed):
                data = K.setup_bytes(stub, pkg.read_bytes(), '0.3.7', uninstaller)
                data = fake_sign(data) if outer_signed else data
                (self.root / 's.exe').write_bytes(data)
                K.verify_setup(self.root / 's.exe', pkg, info, uninstaller, signed=outer_signed)
                lay = K.setup_layout(data)
                u_offset, u_size = lay['uninstaller']
                self.assertEqual((u_offset, u_size), (len(stub) + len(pkg.read_bytes()), len(uninstaller)))
                self.assertEqual(data[u_offset:u_offset + u_size], uninstaller)
                with self.assertRaises(SystemExit):   # the caller expected none (or another one)
                    K.verify_setup(self.root / 's.exe', pkg, info)
        other = fake_sign(fake_setup_stub(identity=f'0.3.6 {SHA} release'))
        changed = bytearray(uninstaller)
        changed[0x210] ^= 1
        for label, blob in {'the stub of another version': other, 'not signed': stub + b'\0' * 8, 'the bare stub': stub,
                            'one byte of the image changed': bytes(changed), 'an overlay before the signature': fake_sign(stub + b'12345678')}.items():
            with self.subTest(label), self.assertRaises(SystemExit):
                K.check_signed_stub(stub, blob)
        with self.assertRaises(SystemExit):   # write_setup refuses to embed anything but this stub, signed
            (self.root / 'app-setup').mkdir(exist_ok=True)
            self.stub_path().write_bytes(stub)
            K.write_setup(self.stub_path(), pkg, info, self.root / 'refused.exe', other)
        self.assertFalse((self.root / 'refused.exe').exists())

    def test_signing_options_are_refused_when_they_make_no_sense(self):
        for extra in (['--sign-expect', 'someone'], ['--timestamp-url', 'http://example.invalid'],
                      ['--sign-name', 'a', '--sign-pfx', 'b.pfx']):
            with self.subTest(extra=extra), self.assertRaises(SystemExit), contextlib.redirect_stdout(io.StringIO()), \
                    contextlib.redirect_stderr(io.StringIO()), \
                    mock.patch.object(sys, 'argv', ['package.py', '--app', str(self.app), '--out', str(self.root / 'dist'), *extra]):
                K.main()
        info = json.loads((self.app / 'version.json').read_text())
        self.assertEqual(info['channel'], 'release')
        pfx = self.root / 'test.pfx'
        pfx.write_bytes(b'not a certificate')
        with self.assertRaises(SystemExit) as refused, contextlib.redirect_stdout(io.StringIO()), \
                mock.patch.object(sys, 'argv', ['package.py', '--app', str(self.app), '--out', str(self.root / 'dist'),
                                                '--sign-pfx', str(pfx), '--sign-expect', 'test']):
            K.main()
        self.assertIn('test certificate never signs a release', str(refused.exception))
        self.assertFalse((self.root / 'dist').exists())

    def test_write_setup_never_overwrites(self):
        dist = package_dist(self.root, self.app)
        with self.assertRaises(SystemExit):
            K.write_setup(self.stub_path(), dist / 'browser-0.3.7-win-x64.lsrupd',
                          json.loads((self.app / 'version.json').read_text()), dist / SETUP)


class SetupPublisher(Publisher):
    """The setup is a required release asset exactly like the ZIP."""

    def test_notes_lead_with_the_setup(self):
        fake = FakeGitHub(existing=[{'tag_name': 'v0.3.6', 'draft': False, 'latest': True, 'assets': []}])
        code, out = self.run_publish(fake)
        self.assertEqual(code, 0)
        create = next(c for c in fake.calls if c[:2] == ('release', 'create'))
        self.assertIn(str(self.d / SETUP), create)
        url = f'https://github.com/{REPO}/releases/download/v0.3.7/{SETUP}'
        self.assertIn(url, fake.notes)
        self.assertLess(fake.notes.index(SETUP), fake.notes.index('browser-0.3.7-win-x64.zip'))
        self.assertIn('LOCALAPPDATA', fake.notes)
        self.assertIn(json.loads((self.d / 'release-info.json').read_text())['setupSha256'], fake.notes)
        self.assertIn(f'Public download: {url}', out)

    def test_missing_or_tampered_setup_never_uploaded(self):
        def tamper():
            f = self.d / SETUP
            f.write_bytes(f.read_bytes()[:-1] + b'?')

        def regenerate_consistently():
            # A self-consistent artifact (release-info + sums regenerated) whose setup embeds ANOTHER package.
            f = self.d / SETUP
            data = bytearray(f.read_bytes())
            data[-200] ^= 1
            f.write_bytes(bytes(data))
            self.edit_info(lambda i: i.update(setupSize=len(data), setupSha256=hashlib.sha256(data).hexdigest()))
            names = ['browser-0.3.7-win-x64.lsrupd', 'update-manifest-win-x64.txt', 'browser-0.3.7-win-x64.zip', SETUP]
            (self.d / 'SHA256SUMS.txt').write_text(''.join(
                f'{hashlib.sha256((self.d / n).read_bytes()).hexdigest()}  {n}\n' for n in names), newline='\n')
        cases = {
            'no setup in release-info': lambda: self.edit_info(lambda i: i.pop('setup')),
            'setup file absent': lambda: (self.d / SETUP).unlink(),
            'setup of another version': lambda: self.edit_info(lambda i: i.update(setup='browser-0.3.6-win-x64-setup.exe')),
            'setup bytes changed': tamper,
            'sums without the setup': lambda: (self.d / 'SHA256SUMS.txt').write_text(
                ''.join(l + '\n' for l in (self.d / 'SHA256SUMS.txt').read_text().splitlines()[:3]), newline='\n'),
            'setup embeds another package': regenerate_consistently,
        }
        for label, mutate in cases.items():
            with self.subTest(label):
                shutil.rmtree(self.d)
                shutil.copytree(self.dist, self.d)
                mutate()
                fake = FakeGitHub(existing=[{'tag_name': 'v0.3.6', 'draft': False, 'latest': True, 'assets': []}])
                with self.assertRaises(SystemExit):
                    self.run_publish(fake)
                self.assertEqual(fake.mutations(), [], label)

    def test_server_setup_digest_mismatch_never_published(self):
        fake = FakeGitHub(existing=[{'tag_name': 'v0.3.6', 'draft': False, 'latest': True, 'assets': []}],
                          corrupt_upload=SETUP)
        with self.assertRaises(SystemExit):
            self.run_publish(fake)
        self.assertTrue(all(r['draft'] for r in fake.releases if r['tag_name'] == 'v0.3.7'))

    def test_public_setup_mismatch_fails(self):
        fake = FakeGitHub(existing=[{'tag_name': 'v0.3.6', 'draft': False, 'latest': True, 'assets': []}],
                          corrupt_public=SETUP)
        with self.assertRaises(SystemExit):
            self.run_publish(fake)

    def test_rerun_after_failed_public_verification_keeps_failing_until_public_bytes_are_right(self):
        """Review P2 (eb3031c): a rerun goes through verify-existing, which must verify the anonymous downloads too."""
        for broken in (SETUP, 'browser-0.3.7-win-x64.zip', 'update-manifest-win-x64.txt'):
            with self.subTest(broken=broken):
                fake = FakeGitHub(existing=[{'tag_name': 'v0.3.6', 'draft': False, 'latest': True, 'assets': []}],
                                  corrupt_public=broken)
                with self.assertRaises(SystemExit):
                    self.run_publish(fake)  # first run: published, public check fails
                self.assertFalse(next(r for r in fake.releases if r['tag_name'] == 'v0.3.7')['draft'])
                for _ in range(2):  # reruns while the public bytes are still wrong
                    before = len(fake.mutations())
                    with self.assertRaises(SystemExit):
                        self.run_publish(fake)
                    # never replaces or adds assets on the recovery path (at most re-marks latest)
                    self.assertTrue(all(c[:2] == ('release', 'edit') for c in fake.mutations()[before:]), fake.mutations())
                fetched = []
                real_fetch = fake.fetch
                fake.fetch = lambda url, limit=4096: (fetched.append(url), real_fetch(url, limit))[1]
                fake.corrupt_public = None  # the CDN now serves the right bytes
                code, out = self.run_publish(fake)
                self.assertEqual(code, 0)
                self.assertTrue(any(u.endswith('/releases/latest/download/update-manifest-win-x64.txt') for u in fetched))
                self.assertTrue(any(u.endswith(f'/v0.3.7/{SETUP}') for u in fetched))
                self.assertTrue(any(u.endswith('/v0.3.7/browser-0.3.7-win-x64.zip') for u in fetched))
                self.assertIn('verified', out)

    def test_dry_run_verify_existing_never_edits(self):
        fake = FakeGitHub(existing=[{'tag_name': 'v0.3.6', 'draft': False, 'latest': True, 'assets': []}])
        self.assertEqual(self.run_publish(fake)[0], 0)
        before = len(fake.mutations())
        self.assertEqual(self.run_publish(fake, '--dry-run')[0], 0)
        self.assertEqual(len(fake.mutations()), before)
        fake.corrupt_public = SETUP
        with self.assertRaises(SystemExit):
            self.run_publish(fake, '--dry-run')  # read-only checks still catch a wrong public setup
        self.assertEqual(len(fake.mutations()), before)

    def test_existing_release_without_setup_refused(self):
        manifest = (self.d / 'update-manifest-win-x64.txt').read_bytes()
        fake = FakeGitHub(existing=[self.existing_release(with_setup=False, with_sums=True)])
        fake.blobs[('v0.3.7', 'update-manifest-win-x64.txt')] = manifest
        with self.assertRaises(SystemExit):
            self.run_publish(fake)
        self.assertEqual(fake.mutations(), [])
        ok = FakeGitHub(existing=[self.existing_release(with_sums=True)])
        ok.blobs[('v0.3.7', 'update-manifest-win-x64.txt')] = manifest
        code, _ = self.run_publish(ok)
        self.assertEqual(code, 0)


class Workflow(unittest.TestCase):
    def test_workflow_security_properties(self):
        text = (ROOT / '.github/workflows/update-release.yml').read_text(encoding='utf-8')
        uses = re.findall(r'uses:\s*(\S+)', text)
        self.assertTrue(uses)
        for u in uses:
            self.assertRegex(u, r'^actions/[a-z-]+@[0-9a-f]{40}$', 'actions must be official and SHA-pinned')
        self.assertIn('permissions:\n  contents: read', text)
        self.assertNotRegex(text, r'contents:\s*write')
        self.assertIn('fetch-depth: 0', text)
        self.assertIn('cancel-in-progress: true', text)
        self.assertIn("vars.UPDATES_PUBLISH_ENABLED == 'true'", text)
        self.assertIn('secrets.UPDATES_RELEASE_TOKEN', text)
        # SEC-03: the token comes from the main-only "release" environment, only in the publish job's last step,
        # and no job output is pasted into a shell script.
        publish = text[text.index('\n  publish:'):]
        self.assertIn('environment:\n      name: release', publish)
        self.assertEqual(publish.count('secrets.'), 1)
        self.assertGreater(text.index('secrets.UPDATES_RELEASE_TOKEN'), text.rindex('- name:'))
        # Code signing (design/installer-signing-options.md): OFF unless the repository variable asks for it; its two
        # secrets exist only in the `sign` job (release environment, main only), each given once, to one step.
        build, sign = text[text.index('\n  build:'):text.index('\n  sign:')], text[text.index('\n  sign:'):text.index('\n  publish:')]
        self.assertNotIn('secrets.', build)
        self.assertEqual(sorted(re.findall(r'secrets\.([A-Z_]+)', text)),
                         ['CERTUM_SIMPLYSIGN_ID', 'CERTUM_SIMPLYSIGN_TOTP_SECRET', 'UPDATES_RELEASE_TOKEN'])
        self.assertEqual(sorted(re.findall(r'secrets\.([A-Z_]+)', sign)), ['CERTUM_SIMPLYSIGN_ID', 'CERTUM_SIMPLYSIGN_TOTP_SECRET'])
        self.assertIn('environment:\n      name: release', sign)
        self.assertIn("if: github.ref == 'refs/heads/main' && github.repository == 'VALXXII/browser' && "
                      "(vars.RELEASE_SIGNING == 'rehearsal' || vars.RELEASE_SIGNING == 'on')", sign)
        self.assertIn('cancel-in-progress: false', sign)
        self.assertEqual(sign.count('simplysign_session.ps1 -Open'), 1)
        self.assertIn("if: always()\n        shell: pwsh\n        run: ./tools/release/simplysign_session.ps1 -Close", sign)
        self.assertLess(sign.index('simplysign_session.ps1 -Open'), sign.index('--sign-name'))
        self.assertNotIn('--sign-pfx', text, 'a test certificate never signs in the release workflow')
        self.assertNotIn('--sign-name', build)
        # Every signing step of the build job is behind the same variable; without it the build job is as before.
        self.assertEqual(build.count('vars.RELEASE_SIGNING'), 2)
        # Signing on: only the signed assets are published, and publish.py is told to refuse anything else.
        self.assertIn("(needs.sign.result == 'success' || vars.RELEASE_SIGNING != 'on')", publish)
        self.assertIn("win-x64${{ vars.RELEASE_SIGNING == 'on' && '-signed' || '' }}", publish)
        self.assertIn('--require-signer "$RELEASE_SIGNER_NAME"', publish)
        self.assertIn('needs: [build, sign]', publish)
        self.assertNotRegex(text, r'run:[^\n]*\$\{\{')
        self.assertNotIn('GITHUB_TOKEN }}', text)
        self.assertNotRegex(text, r'gh[pousr]_[A-Za-z0-9]{20,}')
        self.assertNotIn('ExecutionPolicy', text)
        self.assertIn('branches: [main]', text)
        self.assertIn('workflow_dispatch', text)
        # publish runs only after build (which runs tests and packaging)
        self.assertIn('needs: build', text)
        self.assertLess(text.index('run_all.py'), text.index('package.py'))


class SignatureCheck(unittest.TestCase):
    """tools/release/sign.py reads signatures with Windows PowerShell: also when a PowerShell 7 shell started us."""

    def test_signature_info_survives_a_powershell_7_module_path(self):
        if os.name != 'nt':
            self.skipTest('Windows only')
        import sign as S
        with tempfile.TemporaryDirectory() as tmp:
            # What PowerShell 7 leaves in PSModulePath for its children: a Microsoft.PowerShell.Security that Windows
            # PowerShell 5.1 finds first and cannot load (the release run of 27 Nov 2026).
            module = Path(tmp) / 'Modules' / 'Microsoft.PowerShell.Security'
            module.mkdir(parents=True)
            (module / 'Microsoft.PowerShell.Security.psd1').write_text(
                "@{ ModuleVersion='7.0.0.0'; GUID='a94c8c7e-9810-47c0-b8af-65089c13a35a'; CompatiblePSEditions=@('Core');"
                " PowerShellVersion='7.0'; CmdletsToExport=@('Get-AuthenticodeSignature') }", encoding='utf-8')
            target = Path(tmp) / 'unsigned.exe'
            target.write_bytes(fake_setup_stub())
            poisoned = str(module.parent) + os.pathsep + os.environ.get('PSModulePath', '')
            with mock.patch.dict(os.environ, {'PSModulePath': poisoned}):
                raw = subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-Command',
                                      f"(Get-AuthenticodeSignature -LiteralPath '{target}').Status"], capture_output=True, text=True)
                if raw.returncode == 0 and 'could not be loaded' not in (raw.stdout + raw.stderr):
                    self.skipTest('this machine does not reproduce the PowerShell 7 module path problem')
                facts = S.signature_info([target])   # must not raise: its own Windows PowerShell environment
            self.assertEqual(len(facts), 1)
            self.assertEqual(facts[0]['status'], 'NotSigned')
            self.assertEqual(facts[0]['signer'], '')


class SigningSession(unittest.TestCase):
    """tools/release/simplysign_session.ps1: the parts that can be proven without an account."""
    SCRIPT = ROOT / 'tools/release/simplysign_session.ps1'

    def test_one_time_codes_match_rfc_6238(self):
        if os.name != 'nt' or not shutil.which('powershell'):
            self.skipTest('Windows PowerShell only')
        s256 = 'GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQGEZA===='   # RFC 6238 appendix B, the 32-byte seed
        s1 = 'GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ'                               # the 20-byte seed
        cases = [(f'otpauth://totp/x?secret={s1}&algorithm=SHA1&digits=8&period=30', 59, '94287082'),
                 (f'otpauth://totp/x?secret={s256}&algorithm=SHA256&digits=8&period=30', 59, '46119246'),
                 (f'otpauth://totp/x?secret={s256}&algorithm=SHA256&digits=8&period=30', 1111111109, '68084774'),
                 (f'otpauth://totp/x?secret={s256}&algorithm=SHA256&digits=8&period=30', 20000000000, '77737706'),
                 (f'otpauth://totp/x?secret={s256}&algorithm=SHA-256&digits=6', 59, '119246'),
                 (s256, 59, '119246')]   # a bare secret: SHA-256, 6 digits, 30 s
        program = ("$ErrorActionPreference='Stop';"
                   "$ast=[System.Management.Automation.Language.Parser]::ParseFile($args[0],[ref]$null,[ref]$null);"
                   "$fn=$ast.Find({param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Get-Totp'},$true);"
                   "Invoke-Expression $fn.Extent.Text;"
                   + ''.join(f"Get-Totp '{secret}' {at};" for secret, at, _ in cases))
        import sign as S
        powershell, env = S.windows_powershell()   # its own module path, whatever shell started the tests
        r = subprocess.run([powershell, '-NoProfile', '-NonInteractive', '-Command', '& {' + program + "} '" + str(self.SCRIPT) + "'"],
                           capture_output=True, text=True, env=env)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout.split(), [want for _, _, want in cases])

    def test_script_is_pinned_and_keeps_secrets_out(self):
        text = self.SCRIPT.read_text(encoding='utf-8')
        self.assertRegex(text, r"\$InstallerSha256 = '[0-9a-f]{64}'")
        self.assertIn("$signature.Status -ne 'Valid'", text)
        self.assertIn('GITHUB_ACTIONS', text, 'never on a developer machine')
        for secret in ('CERTUM_SIMPLYSIGN_ID', 'CERTUM_SIMPLYSIGN_TOTP_SECRET'):
            for line in text.splitlines():
                if secret in line and not line.lstrip().startswith('#'):
                    self.assertNotRegex(line, r'Write-(Host|Output)', f'{secret} must never be printed')
        self.assertNotRegex(text, r'Write-(Host|Output)[^\n]*Get-Totp')


class ReleaseNotesFile(unittest.TestCase):
    """release-notes/notes.json -> <out>-obj/gen/release-notes-data.h (design/release-notes-fix-report.md)."""
    GOOD = {'ajouts': [{'fr': 'Onglets sur le côté.', 'en': 'Tabs on the side.'}],
            'corrections': [{'fr': 'La bulle suit l’élément.', 'en': 'The bubble follows the element.'}]}

    def notes(self, data):
        self.assertEqual(R.check(data), [])
        return {s: [(i['fr'], i['en']) for i in data[s]] for s in R.SECTIONS}

    def test_committed_notes_are_valid(self):
        notes = R.load()
        self.assertTrue(R.notes_id(notes).startswith('notes-'), 'the next release has notes')
        for section in R.SECTIONS:
            self.assertLessEqual(len(notes[section]), R.MAX_ITEMS)

    def test_id_follows_the_text(self):
        a = self.notes(self.GOOD)
        self.assertRegex(R.notes_id(a), r'^notes-[0-9a-f]{16}$')
        self.assertEqual(R.notes_id(a), R.notes_id(self.notes(json.loads(json.dumps(self.GOOD)))))
        changed = json.loads(json.dumps(self.GOOD)); changed['corrections'][0]['en'] = 'The bubble follows the element!'
        self.assertNotEqual(R.notes_id(a), R.notes_id(self.notes(changed)), 'new text = new id = the card opens again')
        self.assertEqual(R.notes_id(self.notes({'ajouts': [], 'corrections': []})), '', 'no notes: no card')

    def test_rules(self):
        def errors(mutate):
            data = json.loads(json.dumps(self.GOOD)); mutate(data); return R.check(data)
        self.assertTrue(errors(lambda d: d['ajouts'].extend([self.GOOD['ajouts'][0]] * R.MAX_ITEMS)))  # one line too many
        self.assertTrue(errors(lambda d: d['ajouts'][0].update(fr='x' * (R.MAX_CHARS + 1))))          # too long
        self.assertTrue(errors(lambda d: d['ajouts'][0].update(fr="L'onglet.")))                       # straight apostrophe
        self.assertTrue(errors(lambda d: d['ajouts'][0].update(en=d['ajouts'][0]['fr'])))              # not translated
        self.assertTrue(errors(lambda d: d['ajouts'][0].update(en='')))                                # empty
        self.assertTrue(errors(lambda d: d['ajouts'][0].update(fr='Deux\nlignes.')))                   # control character
        self.assertTrue(errors(lambda d: d['ajouts'][0].update(fr=' Espace.')))                        # stray space
        self.assertTrue(errors(lambda d: d['ajouts'][0].update(de='Tabs.')))                           # unknown language
        self.assertTrue(errors(lambda d: d.update(correction=[])))                                     # misspelt section
        self.assertTrue(errors(lambda d: d.pop('corrections')))                                        # missing section
        self.assertTrue(R.check([]))
        self.assertEqual(errors(lambda d: d.update(ajouts=[])), [], 'an empty section is allowed (not drawn)')

    def test_header_is_ascii_and_escaped(self):
        notes = {'ajouts': [('Guillemets " et \\ et ?? et « é » ’ 😀', 'Quote " ok')], 'corrections': []}
        text = R.header([{'build': 950, 'version': '0.3.950', 'commit': None, 'notes': notes, 'id': R.notes_id(notes)}])
        text.encode('ascii')
        self.assertIn(f'{{"0.3.950", "{R.notes_id(notes)}", kAdded0, kFixed0}},', text)
        self.assertIn('L"Guillemets \\" et \\\\ et \\?\\? et \\u00AB \\u00E9 \\u00BB \\u2019 \\U0001F600"', text)
        self.assertIn('inline const Line kFixed0[] = { {nullptr, nullptr}};', text)
        self.assertIn('#define BROWSER_RELEASE_NOTES_HISTORY 1', text)
        empty = R.header([])
        self.assertIn('{nullptr, nullptr, nullptr, nullptr},', empty)

    def repo(self, tmp):
        """A small repository whose main grows like the real one: direct commits and --no-ff merges."""
        root = Path(tmp)
        def git(*args):
            subprocess.run(['git', '-c', 'user.name=t', '-c', 'user.email=t@example.com', *args], cwd=root, check=True, capture_output=True)
        def notes(tag, n=1):
            return json.dumps({'ajouts': [{'fr': f'{tag} n° {k}.', 'en': f'{tag} no. {k}.'} for k in range(n)], 'corrections': []}, ensure_ascii=False)
        def commit(message, text=None, other=None):
            if text is not None:
                (root / 'release-notes').mkdir(exist_ok=True); (root / R.REL).write_text(text, encoding='utf-8')
            if other is not None:
                (root / 'other.txt').write_text(other, encoding='utf-8')
            git('add', '-A'); git('commit', '-q', '-m', message)
        git('init', '-q', '-b', 'main')
        commit('1 other', other='a')                                   # 1
        commit('2 notes A', text=notes('A', 2))                        # 2: version 0.3.2
        git('checkout', '-q', '-b', 'feature')
        commit('3 notes B on a branch', text=notes('B'))               # 3 (branch)
        git('checkout', '-q', 'main')
        commit('4 other on main', other='b')                           # 4
        git('merge', '-q', '--no-ff', 'feature', '-m', '5 merge B')    # 5: version 0.3.5, not 0.3.3
        commit('6 no notes change', other='c')                         # 6: no entry
        commit('7 bad notes', text='{"ajouts": [{"fr": "x", "en": "x"}], "corrections": []}')   # 7: invalid, skipped
        commit('8 notes C', text=notes('C', 3))                        # 8: version 0.3.8
        return root, notes

    def test_history_per_version_from_git(self):
        with tempfile.TemporaryDirectory() as tmp:
            root, notes = self.repo(tmp)
            warned = []
            h = R.history(root, warn=warned.append)
            self.assertEqual([e['version'] for e in h], ['0.3.8', '0.3.5', '0.3.2'], 'one entry per version that changed the notes, the merge (first parent) giving the version')
            self.assertEqual(h[1]['notes']['ajouts'], [('B n° 0.', 'B no. 0.')])
            self.assertTrue(any('0.3.7' in w for w in warned), 'an invalid old version is skipped, said')
            self.assertEqual([e['version'] for e in R.history(root, head='HEAD~3', worktree=False)], ['0.3.5', '0.3.2'])
            # Not committed yet: the next release's notes (after HEAD), or this build's own (a build of this tree).
            (root / R.REL).write_text(notes('D'), encoding='utf-8')
            self.assertEqual([e['version'] for e in R.history(root)], ['0.3.9', '0.3.8', '0.3.5', '0.3.2'])
            self.assertEqual([e['version'] for e in R.history(root, build=8)][:2], ['0.3.8', '0.3.5'])
            self.assertEqual(R.history(root, build=8)[0]['notes']['ajouts'], [('D n° 0.', 'D no. 0.')])
            # The generated header holds every version up to the build.
            with contextlib.redirect_stdout(io.StringIO()):
                entries = R.write_header(root / 'gen' / 'release-notes-data.h', 5, root)
            self.assertEqual([e['version'] for e in entries], ['0.3.5', '0.3.2'])
            text = (root / 'gen' / 'release-notes-data.h').read_text(encoding='ascii')
            self.assertIn('{"0.3.5", "', text); self.assertNotIn('"0.3.8"', text)

    def test_cumulative_selection(self):
        def entry(build, added, fixed=()):
            n = {'ajouts': [(a, a + ' (en)') for a in added], 'corrections': [(f, f + ' (en)') for f in fixed]}
            return {'build': build, 'version': R.version_text(build), 'commit': None, 'notes': n, 'id': R.notes_id(n)}
        h = [entry(842, ['Print', 'Share', 'Emoji'], ['Bubble']), entry(898, ['Side tabs']), entry(950, ['Focus', 'Print'], ['Downloads', 'History'])]
        firsts = lambda sel, s: [p[0] for p in sel[s]]
        sel = R.select(h, 646, 950)
        self.assertEqual(firsts(sel, 'ajouts'), ['Focus', 'Print', 'Side tabs', 'Share', 'Emoji'], 'newest first, once')
        self.assertEqual(firsts(R.select(h, 646, 950, 4), 'ajouts'), ['Focus', 'Print', 'Side tabs', 'Share'], 'a cap keeps the newest')
        self.assertEqual(R.DISPLAY_LINES, 10)
        self.assertEqual(firsts(sel, 'corrections'), ['Downloads', 'History', 'Bubble'])
        self.assertEqual(firsts(R.select(h, 898, 950), 'ajouts'), ['Focus', 'Print'], 'only what was not seen')
        self.assertEqual(firsts(R.select(h, 898, 920), 'ajouts'), [], 'versions without notes: nothing (no card)')
        self.assertEqual(firsts(R.select(h, None, 900), 'ajouts'), ['Side tabs', 'Print', 'Share', 'Emoji'], 'never newer than the build')
        self.assertEqual(len(R.select(h, None, 950, 0)['ajouts']), 5)

    def test_real_history_has_the_published_notes(self):
        try:
            h = R.history(head='5056b8b71e7d26f05b55fcd253b2353c2232eec3', worktree=False)   # main at 0.3.898
        except SystemExit:
            self.skipTest('no full history here')
        if not h:
            self.skipTest('no full history here')
        self.assertEqual([e['version'] for e in h], ['0.3.898', '0.3.842'], 'the two releases that carried notes.json')

    def test_build_generates_the_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            gen = Path(tmp)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                B.write_release_notes(gen, V.identity(842, SHA, 'dev'))
            text = (gen / 'release-notes-data.h').read_text(encoding='ascii')
            self.assertIn('#define BROWSER_RELEASE_NOTES_HISTORY 1', text)
            self.assertNotIn('"0.3.898"', text, 'nothing newer than the build')
            self.assertIn('Release notes:', out.getvalue())

    def test_release_build_warns_when_the_merge_kept_the_notes(self):
        for same, warned in ((True, True), (False, False)):
            def git(*args, **_):
                return 'same' if same or args[1].startswith(SHA + '^1') else 'changed'
            out = io.StringIO()
            with tempfile.TemporaryDirectory() as tmp, mock.patch.object(B.version_mod, 'git', git), contextlib.redirect_stdout(out):
                B.write_release_notes(Path(tmp), V.identity(42, SHA, 'release'))
            self.assertEqual('unchanged by this merge' in out.getvalue(), warned, out.getvalue())

if __name__ == '__main__':
    unittest.main(verbosity=2)
