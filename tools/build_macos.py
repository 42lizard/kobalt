#!/usr/bin/env python3
"""Build a self-contained Kobalt.app for this Mac."""
import argparse
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

import assets
import fetch_fonts
from run_tests import ROOT, HOST, DEFAULT_CACHE, build_qt, build_engine
from session_lock import acquire
from version import release_version


def run(*command):
    subprocess.run([str(part) for part in command], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--cache', type=Path, default=DEFAULT_CACHE)
    parser.add_argument('--offline', action='store_true')
    parser.add_argument('--qt', type=Path, help='existing host Qt SDK with bundled libpng')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/macos')
    parser.add_argument('--dependencies', type=Path, default=ROOT / 'build/tests' / HOST)
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('Build Kobalt.app on macOS')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    for command in ('cmake', 'ninja', 'ctest', 'pkg-config', 'patch', 'iconutil', 'codesign'):
        if not shutil.which(command):
            parser.error(f'{command} is required; see docs/development.md')
    output, dependencies = args.output.resolve(), args.dependencies.resolve()
    output.mkdir(parents=True, exist_ok=True)
    dependencies.mkdir(parents=True, exist_ok=True)
    version = release_version()
    fetch_fonts.fetch(ROOT, offline=args.offline)
    with acquire(output / 'build.lock', wait=True):
        # Share the host SDK with tools/run_tests.py without concurrent writes.
        with acquire(dependencies / 'build.lock', wait=True):
            qt = args.qt.resolve() if args.qt else build_qt(dependencies, args.jobs, args.cache.resolve(), args.offline)
            engine = build_engine(qt, dependencies, args.jobs, args.cache.resolve(), args.offline)
        staged = assets.stage(output / 'assets', version=version)
        fixtures = output / 'fixtures'
        run(sys.executable, '-B', ROOT / 'tests/fixtures/generate.py', fixtures)
        commit, modified = '', False
        try:
            commit = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
            modified = bool(subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True).strip())
        except (OSError, subprocess.CalledProcessError):
            pass
        build = output / 'build'
        run('cmake', '-S', ROOT / 'desktop', '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
            '-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0', f'-DCMAKE_OSX_ARCHITECTURES={platform.machine()}',
            f'-DCMAKE_PREFIX_PATH={qt}', f'-DKOBALT_VERSION={version}',
            f'-DKOBALT_BUNDLE_VERSION={re.match(r"v([0-9.]+)", version)[1]}',
            f'-DKOBALT_COMMIT={commit}', f'-DKOBALT_COMMIT_MODIFIED={"ON" if modified else "OFF"}',
            f'-DKOBALT_DESKTOP_ASSETS={staged}', f'-DKOBALT_DESKTOP_FIXTURES={fixtures}', *engine)
        run('cmake', '--build', build, '--parallel', args.jobs)
        run('ctest', '--test-dir', build, '--output-on-failure')
        # Package a fresh copy, so removed assets and Qt plugins cannot survive
        # an incremental rebuild. The compiler's bundle remains untouched.
        app = output / 'Kobalt.app'
        if app.exists():
            shutil.rmtree(app)
        shutil.copytree(build / 'Kobalt.app', app)
        resources = app / 'Contents/Resources'
        resources.mkdir(exist_ok=True)
        shutil.copytree(staged, resources / 'assets')
        iconset = output / 'Kobalt.iconset'
        iconset.mkdir(exist_ok=True)
        run(build / 'kobalt-icon', ROOT / 'docs/assets/kobalt-android.svg', iconset)
        run('iconutil', '-c', 'icns', '-o', resources / 'Kobalt.icns', iconset)
        run(qt / 'bin/macdeployqt', app, '-always-overwrite')
        # Local development signature. Distribution signing and notarization
        # belong to a release workflow, not to this testing app.
        run('codesign', '--force', '--deep', '--sign', '-', app)
        run('codesign', '--verify', '--deep', '--strict', app)
        print(f'Built {app}')


if __name__ == '__main__':
    main()
