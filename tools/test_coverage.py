#!/usr/bin/env python3
"""Report native source coverage without treating unbuilt platform code as covered."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
METRICS = ('lines', 'branches', 'functions', 'regions')


def llvm_tool(name):
    found = shutil.which(name)
    if found:
        return [found]
    if sys.platform == 'darwin' and shutil.which('xcrun'):
        return ['xcrun', name]
    raise ValueError(name + ' is required to report Clang coverage')


def test_binaries(build):
    # Even --show-only replaces CTest's last-run log. Keep the test evidence.
    log = build / 'Testing/Temporary/LastTest.log'
    previous = log.read_bytes() if log.is_file() else None
    try:
        inventory = json.loads(subprocess.check_output(
            ['ctest', '--test-dir', str(build), '--show-only=json-v1'], text=True))
    finally:
        if previous is not None:
            log.write_bytes(previous)
        else:
            log.unlink(missing_ok=True)
    binaries = set()
    for test in inventory['tests']:
        command = test.get('command', [])
        if command:
            binary = Path(command[0]).resolve()
            if binary.is_relative_to(build.resolve()) and binary.is_file():
                binaries.add(binary)
    if not binaries:
        raise ValueError('No native test executables in ' + str(build))
    return sorted(binaries)


def summarize(export, root):
    source = (root / 'src').resolve()
    files = {}
    for data in export['data']:
        for item in data['files']:
            path = Path(item['filename']).resolve()
            if not path.is_relative_to(source):
                continue
            name = path.relative_to(root.resolve()).as_posix()
            if name in files:
                raise ValueError('Duplicate source summary: ' + name)
            files[name] = {metric: item['summary'][metric] for metric in METRICS}
    if not files:
        raise ValueError('No production source coverage in the LLVM export')
    totals = {}
    for metric in METRICS:
        count = sum(item[metric]['count'] for item in files.values())
        covered = sum(item[metric]['covered'] for item in files.values())
        totals[metric] = dict(count=count, covered=covered,
                              percent=100 * covered / count if count else None)
    inventory = sorted(path.relative_to(root.resolve()).as_posix()
                       for path in source.rglob('*')
                       if path.is_file() and path.suffix in ('.c', '.cc', '.cpp', '.cxx', '.h', '.hpp'))
    digest = hashlib.sha256()
    for name in inventory:
        digest.update(name.encode() + b'\0' + (root / name).read_bytes() + b'\0')
    return dict(scope='Instrumented public src/ files in registered native tests; '
                      'unmeasured files are listed separately and are not in the percentages.',
                source_sha256=digest.hexdigest(), totals=totals, files=dict(sorted(files.items())),
                unmeasured_files=[name for name in inventory if name not in files])


def report(build, profiles, output, root=ROOT):
    metadata_path = profiles / 'run.json'
    metadata = json.loads(metadata_path.read_text()) if metadata_path.is_file() else {}
    raw_profiles = sorted(profiles.glob('*.profraw'))
    if not raw_profiles:
        raise ValueError('No raw profiles from this test run')
    binaries = test_binaries(build)
    output.mkdir(parents=True, exist_ok=True)
    merged = output / 'merged.profdata'
    subprocess.run([*llvm_tool('llvm-profdata'), 'merge', '-sparse',
                    *map(str, raw_profiles), '-o', str(merged)], check=True)
    command = [*llvm_tool('llvm-cov'), 'export', str(binaries[0]),
               *['-object=' + str(binary) for binary in binaries[1:]],
               '-instr-profile=' + str(merged), '-skip-functions', '-skip-expansions']
    exported = subprocess.run(command, check=True, capture_output=True, text=True)
    export = json.loads(exported.stdout)
    result = summarize(export, root)
    result['binary_sha256'] = {str(binary.relative_to(build)): hashlib.sha256(binary.read_bytes()).hexdigest()
                               for binary in binaries}
    previous_path = profiles / 'summary.json'
    if previous_path.is_file():
        previous = json.loads(previous_path.read_text())
        for key in ('source_sha256', 'binary_sha256'):
            if key in previous and previous[key] != result[key]:
                raise ValueError('Source or test binaries changed since this coverage run; run the tests again')
    for key in ('test_exit_code', 'ctest_arguments', 'host', 'variable_font_sha256'):
        if key in metadata:
            result[key] = metadata[key]
    result['llvm_cov_warnings'] = exported.stderr.strip().splitlines()
    if exported.stderr:
        print(exported.stderr.rstrip(), file=sys.stderr)
    result['binaries'] = [str(binary.relative_to(build)) for binary in binaries]
    # Retain production regions and branches so uncovered paths can be reviewed.
    source = (root / 'src').resolve()
    production = [item for data in export['data'] for item in data['files']
                  if Path(item['filename']).resolve().is_relative_to(source)]
    (output / 'regions.json').write_text(json.dumps(production) + '\n')
    destination = output / 'summary.json'
    destination.write_text(json.dumps(result, indent=2) + '\n')
    for metric, value in result['totals'].items():
        percent = 'n/a' if value['percent'] is None else f"{value['percent']:.2f}%"
        print(f"Production {metric}: {value['covered']}/{value['count']} ({percent})")
    print(f"Measured files: {len(result['files'])}; unmeasured files: {len(result['unmeasured_files'])}")
    print('Coverage summary: ' + str(destination))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path, help='instrumented CTest build directory')
    parser.add_argument('--profiles', type=Path, required=True, help='profiles from one test run')
    parser.add_argument('--output', type=Path, required=True, help='report directory')
    args = parser.parse_args()
    try:
        report(args.build.resolve(), args.profiles, args.output)
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
