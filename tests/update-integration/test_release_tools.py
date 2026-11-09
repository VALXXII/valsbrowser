"""Deterministic tests for tools/release (no network, no GitHub mutation).

Covers: build identity (monotonic, shallow refusal, input validation), publish
decision guards (never regress the feed), payload staging allowlist with canary
runtime data that must never be packaged nor modified, manifest parsing, and the
workflow file's security-relevant properties.
"""
from __future__ import annotations
import hashlib, json, re, shutil, subprocess, sys, tempfile, unittest
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
