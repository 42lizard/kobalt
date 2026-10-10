"""Check coverage scope and denominators without requiring an LLVM installation."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import test_coverage


class CoverageTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name).resolve()
        (self.root / 'src').mkdir()
        (self.root / 'src/measured.cpp').write_text('int value() { return 1; }\n')
        (self.root / 'src/unbuilt.cpp').write_text('int other() { return 2; }\n')
        (self.root / 'src/empty.h').write_text('#pragma once\n')

    def entry(self, filename, count=4, covered=1):
        return dict(filename=str(self.root / filename), summary={
            metric: dict(count=count, covered=covered, percent=0)
            for metric in test_coverage.METRICS})

    def export(self, *entries):
        return dict(data=[dict(files=list(entries))])

    def test_excludes_tests_dependencies_and_sibling_sources(self):
        result = test_coverage.summarize(self.export(
            self.entry('src/measured.cpp'), self.entry('tests/check.cpp', 100, 100),
            self.entry('vendor/engine.cpp', 200, 200),
            self.entry('src-sibling/other.cpp', 100, 100)), self.root)
        self.assertEqual(list(result['files']), ['src/measured.cpp'])
        self.assertEqual(result['totals']['lines'], dict(count=4, covered=1, percent=25))
        self.assertEqual(result['unmeasured_files'], ['src/empty.h', 'src/unbuilt.cpp'])

    def test_aggregates_counts_instead_of_averaging_percentages(self):
        result = test_coverage.summarize(self.export(
            self.entry('src/measured.cpp', 2, 2), self.entry('src/unbuilt.cpp', 8, 0)), self.root)
        self.assertEqual(result['totals']['branches'], dict(count=10, covered=2, percent=20))

    def test_zero_denominator_is_not_reported_as_full_coverage(self):
        result = test_coverage.summarize(self.export(self.entry('src/measured.cpp', 0, 0)), self.root)
        self.assertIsNone(result['totals']['functions']['percent'])

    def test_duplicate_files_and_empty_source_export_are_errors(self):
        entry = self.entry('src/measured.cpp')
        with self.assertRaisesRegex(ValueError, 'Duplicate source summary'):
            test_coverage.summarize(self.export(entry, entry), self.root)
        with self.assertRaisesRegex(ValueError, 'No production source coverage'):
            test_coverage.summarize(self.export(self.entry('tests/check.cpp')), self.root)

    def test_source_fingerprint_includes_unmeasured_code(self):
        export = self.export(self.entry('src/measured.cpp'))
        before = test_coverage.summarize(export, self.root)['source_sha256']
        (self.root / 'src/unbuilt.cpp').write_text('int other() { return 3; }\n')
        after = test_coverage.summarize(export, self.root)['source_sha256']
        self.assertNotEqual(before, after)

    def test_missing_profiles_fail_before_running_tools(self):
        with patch.object(test_coverage.subprocess, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'No raw profiles'):
                test_coverage.report(self.root, self.root, self.root, self.root)
            run.assert_not_called()

    def test_ctest_inventory_deduplicates_binaries_and_excludes_python(self):
        binary = self.root / 'reader-test'
        binary.touch()
        inventory = dict(tests=[dict(command=[str(binary), 'first']),
                                dict(command=[str(binary), 'second']),
                                dict(command=[sys.executable, 'check.py']), dict()])
        log = self.root / 'Testing/Temporary/LastTest.log'
        log.parent.mkdir(parents=True)
        log.write_text('Actual test results\n')
        def inspect(*args, **kwargs):
            log.write_text('Empty inventory-run log\n')
            return json.dumps(inventory)
        with patch.object(test_coverage.subprocess, 'check_output', side_effect=inspect):
            self.assertEqual(test_coverage.test_binaries(self.root), [binary])
        self.assertEqual(log.read_text(), 'Actual test results\n')

    def test_report_uses_every_test_binary_and_retains_tool_warnings(self):
        first, second = self.root / 'first-test', self.root / 'second-test'
        first.write_bytes(b'first binary')
        second.write_bytes(b'second binary')
        (self.root / 'one.profraw').touch()
        (self.root / 'two.profraw').touch()
        export = self.export(self.entry('src/measured.cpp'))
        export['data'][0]['files'][0].update(segments=[], branches=[])
        output = self.root
        metadata = dict(test_exit_code=7, ctest_arguments=['-R', 'reader'], host='test-host')
        (self.root / 'run.json').write_text(json.dumps(metadata))
        completed = test_coverage.subprocess.CompletedProcess([], 0,
            stdout=json.dumps(export), stderr='Fixture coverage warning\n')
        with patch.object(test_coverage, 'test_binaries', return_value=[first, second]), \
                patch.object(test_coverage, 'llvm_tool', side_effect=lambda name: [name]), \
                patch.object(test_coverage.subprocess, 'run', return_value=completed) as run:
            result = test_coverage.report(self.root, self.root, output, self.root)
            regenerated = test_coverage.report(self.root, self.root, output, self.root)
            second.write_bytes(b'rebuilt binary')
            with self.assertRaisesRegex(ValueError, 'changed since this coverage run'):
                test_coverage.report(self.root, self.root, output, self.root)
        self.assertEqual(regenerated, result)
        merge, coverage = [call.args[0] for call in run.call_args_list[:2]]
        self.assertIn(str(self.root / 'one.profraw'), merge)
        self.assertIn(str(self.root / 'two.profraw'), merge)
        self.assertIn(str(first), coverage)
        self.assertIn('-object=' + str(second), coverage)
        self.assertEqual(result['llvm_cov_warnings'], ['Fixture coverage warning'])
        for key, value in metadata.items():
            self.assertEqual(result[key], value)
        self.assertEqual(json.loads((output / 'summary.json').read_text()), result)
        self.assertEqual(len(json.loads((output / 'regions.json').read_text())), 1)


if __name__ == '__main__':
    unittest.main()
