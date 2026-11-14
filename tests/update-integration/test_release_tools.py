"""Deterministic tests for tools/release (no network, no GitHub mutation).

Covers: build identity (monotonic, shallow refusal, input validation), publish
decision guards (never regress the feed), payload staging allowlist with canary
runtime data that must never be packaged nor modified, manifest parsing, the public
first-run ZIP (exact staged entries and bytes, checksums, release-info), the
publisher end to end against an in-memory fake GitHub (positive and negative cases),
and the workflow file's security-relevant properties.
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


def make_app(root: Path) -> Path:
    app = root / 'app'
    (app / 'assets/fonts').mkdir(parents=True)
    for name in ('browser.exe', 'update-helper.exe', 'WebView2Loader.dll', 'ThorVG-LICENSE.txt',
                 'WebView2-NOTICE.txt'):
        (app / name).write_bytes(name.encode())
    (app / 'assets/fonts/a.ttf').write_bytes(b'font')
    (app / 'version.json').write_text(json.dumps({'version': '0.3.7', 'arch': 'x64'}))
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
            before = canary(app)
            files = K.stage(app, Path(tmp) / 'stage')
            self.assertEqual(canary(app), before)
            self.assertFalse(any('profile' in f or f in ('session', 'prototype.log') for f in files))
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
            'SHA256SUMS.txt', 'release-info.json']))

    def test_checksums_and_release_info_cover_zip(self):
        dist = package_dist(self.root, self.app)
        archive = dist / 'browser-0.3.7-win-x64.zip'
        sums = (dist / 'SHA256SUMS.txt').read_bytes().decode('ascii')
        lines = dict(reversed(l.split('  ')) for l in sums.splitlines())
        self.assertEqual(set(lines), {'browser-0.3.7-win-x64.lsrupd', 'update-manifest-win-x64.txt', archive.name})
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

    def existing_release(self, with_zip=True, zip_bytes=None):
        assets = []
        for name in ('browser-0.3.7-win-x64.lsrupd', 'update-manifest-win-x64.txt', 'browser-0.3.7-win-x64.zip'):
            if name.endswith('.zip') and not with_zip:
                continue
            data = zip_bytes if (name.endswith('.zip') and zip_bytes is not None) else (self.d / name).read_bytes()
            assets.append({'name': name, 'digest': 'sha256:' + hashlib.sha256(data).hexdigest()})
        return {'tag_name': 'v0.3.7', 'draft': False, 'latest': True, 'assets': assets}

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
        self.assertIn(f'Public download: {url}', out)
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
        fake = FakeGitHub(existing=[self.existing_release()])
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

    def test_superseded_and_dry_run_do_not_mutate(self):
        fake = FakeGitHub(existing=[{'tag_name': 'v0.3.8', 'draft': False, 'latest': True, 'assets': []}])
        self.assertEqual(self.run_publish(fake)[0], 0)
        self.assertEqual(fake.mutations(), [])
        fake = FakeGitHub()
        code, out = self.run_publish(fake, '--dry-run')
        self.assertEqual(code, 0)
        self.assertIn('browser-0.3.7-win-x64.zip', out)
        self.assertEqual(fake.mutations(), [])


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
        self.assertNotIn('GITHUB_TOKEN }}', text)
        self.assertNotRegex(text, r'gh[pousr]_[A-Za-z0-9]{20,}')
        self.assertNotIn('ExecutionPolicy', text)
        self.assertIn('branches: [main]', text)
        self.assertIn('workflow_dispatch', text)
        # publish runs only after build (which runs tests and packaging)
        self.assertIn('needs: build', text)
        self.assertLess(text.index('run_all.py'), text.index('package.py'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
