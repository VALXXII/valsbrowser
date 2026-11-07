"""Build identity for browser releases.

The build number N is the number of commits reachable from HEAD
(`git rev-list --count HEAD`). On main, which only grows by merges and is never
force-pushed, every later state contains every earlier one, so N increases
strictly in merge order. It never compares git hashes. The version is
"<major>.<minor>.<N>" and the release tag "v<version>".

CI must check out with full history (fetch-depth: 0); a shallow clone would give
a wrong, small N, so shallow repositories are refused.
"""
from __future__ import annotations
import argparse, json, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SERIES = (0, 3)  # bump the series by editing this line; N keeps growing anyway.


def git(*args: str, repo: Path = ROOT) -> str:
    return subprocess.run(['git', *args], cwd=repo, check=True, capture_output=True,
                          text=True).stdout.strip()


def identity(build: int | None = None, commit: str | None = None,
             channel: str = 'dev', repo: Path = ROOT) -> dict:
    if build is None:
        if git('rev-parse', '--is-shallow-repository', repo=repo) == 'true':
            raise SystemExit('Shallow clone: the build number would be wrong. Fetch full history.')
        build = int(git('rev-list', '--count', 'HEAD', repo=repo))
    if commit is None:
        commit = git('rev-parse', 'HEAD', repo=repo)
    if not (0 < build < 65535):
        raise SystemExit(f'Build number out of range for a Windows version resource: {build}')
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise SystemExit(f'Unexpected commit id: {commit!r}')
    if channel not in ('dev', 'release'):
        raise SystemExit(f'Unknown channel: {channel}')
    version = f'{SERIES[0]}.{SERIES[1]}.{build}'
    return {'version': version, 'build': build, 'major': SERIES[0], 'minor': SERIES[1],
            'commit': commit, 'tag': f'v{version}', 'channel': channel}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=int)
    p.add_argument('--commit')
    p.add_argument('--channel', default='dev')
    p.add_argument('--github-output', help='append key=value lines (GitHub Actions $GITHUB_OUTPUT)')
    a = p.parse_args()
    info = identity(a.build, a.commit, a.channel)
    print(json.dumps(info, indent=2))
    if a.github_output:
        with open(a.github_output, 'a', encoding='utf-8') as out:
            for key in ('version', 'build', 'commit', 'tag'):
                out.write(f'{key}={info[key]}\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
