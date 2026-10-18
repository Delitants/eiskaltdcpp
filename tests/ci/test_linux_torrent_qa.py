#!/usr/bin/env python3
"""Offline contract tests; these do not qualify a Linux application build."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WorkflowTests(unittest.TestCase):
    def test_focused_workflow_contract(self):
        text = (ROOT / '.github/workflows/torrent-linux-qa.yml').read_text()
        for required in ('workflow_dispatch:', 'contents: read', 'ubuntu-24.04',
                         'timeout-minutes: 60', 'EISKALT_REQUIRE_SELECTED_RECHECK=ON',
                         'USE_TORRENT=ON', 'BUILD_TESTS=ON', '--parallel 2',
                         'eiskalt-selected-recheck-dependency-tests',
                         'build-libtorrent.sh', 'sudo unshare --net', 'runuser -u',
                         'if: always()', 'if-no-files-found: error'):
            self.assertIn(required, text)
        for forbidden in ('contents: write', 'secrets.', 'gh release', 'cmake --install build',
                          'pull_request:', 'push:'):
            self.assertNotIn(forbidden, text)


class EvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location(
            'qa', ROOT / 'tools/ci/verify-linux-torrent-qa.py')
        cls.qa = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.qa)

    def inventory(self):
        return {'tests': [
            {'name': 'core case', 'command': ['/b/tests/eiskaltdcpp-tests', 'case']},
            {'name': 'Qt case', 'command': ['/b/tests/qt/eiskaltdcpp-qt-tests', 'case']},
            {'name': 'torrent-backend', 'command': ['/b/tests/torrent/eiskalt-torrent-tests']}]}

    def test_complete_inventory(self):
        self.assertEqual(len(self.qa.inventory(self.inventory())), 3)

    def test_missing_each_suite_refused(self):
        for index in range(3):
            data = self.inventory()
            del data['tests'][index]
            with self.subTest(index=index), self.assertRaises(ValueError):
                self.qa.inventory(data)

    def test_empty_command_refused(self):
        data = self.inventory()
        data['tests'][0]['command'] = []
        with self.assertRaises(ValueError):
            self.qa.inventory(data)

    def test_duplicate_name_refused(self):
        data = self.inventory()
        data['tests'][1]['name'] = 'core case'
        with self.assertRaises(ValueError):
            self.qa.inventory(data)

    def junit(self, core='', qt='', torrent=''):
        return ('<testsuite><testcase name="core case">' + core + '</testcase>'
                '<testcase name="Qt case">' + qt + '</testcase>'
                '<testcase name="torrent-backend">' + torrent + '</testcase></testsuite>')

    def test_results_complete(self):
        counts = self.qa.results(self.qa.inventory(self.inventory()), self.junit())
        self.assertEqual(counts['passed']['torrent'], 1)

    def test_failure_error_or_skipped_backend_refused(self):
        for child in ('<failure/>', '<error/>', '<skipped/>'):
            with self.subTest(child=child), self.assertRaises(ValueError):
                self.qa.results(self.qa.inventory(self.inventory()), self.junit(torrent=child))

    def test_entire_core_or_qt_skipped_refused(self):
        for kwargs in ({'core': '<skipped/>'}, {'qt': '<skipped/>'}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                self.qa.results(self.qa.inventory(self.inventory()), self.junit(**kwargs))

    def test_partial_qt_skip_reported(self):
        data = self.inventory()
        data['tests'].append({'name': 'opt-in', 'command': ['/b/eiskaltdcpp-qt-tests']})
        xml = self.junit().replace('</testsuite>',
                                  '<testcase name="opt-in"><skipped/></testcase></testsuite>')
        self.assertEqual(self.qa.results(self.qa.inventory(data), xml)['skipped'], ['opt-in'])

    def test_additional_integration_executables_retained(self):
        data = self.inventory()
        for name in ('eiskalt-settings-quit-tests', 'eiskalt-mainwindow-toggle-tests'):
            data['tests'].append({'name': name, 'command': ['/b/' + name]})
        xml = self.junit().replace('</testsuite>',
                                  '<testcase name="eiskalt-settings-quit-tests"/>'
                                  '<testcase name="eiskalt-mainwindow-toggle-tests"/></testsuite>')
        registered = self.qa.inventory(data)
        self.assertEqual(len(registered), 5)
        self.assertEqual(self.qa.results(registered, xml)['passed']['other'], 2)

    def test_additional_integration_failure_refused(self):
        data = self.inventory()
        data['tests'].append({'name': 'settings quit', 'command': ['/b/eiskalt-settings-quit-tests']})
        xml = self.junit().replace('</testsuite>',
                                  '<testcase name="settings quit"><failure/></testcase></testsuite>')
        with self.assertRaises(ValueError):
            self.qa.results(self.qa.inventory(data), xml)

    def test_missing_result_refused(self):
        with self.assertRaises(ValueError):
            self.qa.results(self.qa.inventory(self.inventory()), '<testsuite/>')

    def test_dependency_empty_failed_or_skipped_refused(self):
        for xml in ('<testsuite/>', '<testsuite><testcase><failure/></testcase></testsuite>',
                    '<testsuite><testcase><skipped/></testcase></testsuite>'):
            with self.subTest(xml=xml), self.assertRaises(ValueError):
                self.qa.dependency_results(xml)

    def test_dependency_executed(self):
        self.assertEqual(self.qa.dependency_results('<testsuite><testcase/></testsuite>'), 1)

    def test_config_required(self):
        good = ('USE_TORRENT:BOOL=ON\nBUILD_TESTS:BOOL=ON\n'
                'EISKALT_REQUIRE_SELECTED_RECHECK:BOOL=ON\n'
                'EISKALT_SELECTED_RECHECK_AVAILABLE:INTERNAL=1\n')
        self.qa.configuration(good)
        for line in good.splitlines():
            with self.subTest(line=line), self.assertRaises(ValueError):
                self.qa.configuration(good.replace(line, ''))


class OrchestrationTests(unittest.TestCase):
    def run_fixture(self, mode):
        with tempfile.TemporaryDirectory(prefix='eiskalt-linux-qa-contract-') as directory:
            root = Path(directory)
            (root / 'tools').symlink_to(ROOT / 'tools', target_is_directory=True)
            build = root / 'build-torrent-linux'
            binary = build / 'tests/torrent/eiskalt-selected-recheck-dependency-tests'
            binary.parent.mkdir(parents=True)
            (build / 'CMakeCache.txt').write_text(
                'USE_TORRENT:BOOL=ON\nBUILD_TESTS:BOOL=ON\n'
                'EISKALT_REQUIRE_SELECTED_RECHECK:BOOL=ON\n'
                'EISKALT_SELECTED_RECHECK_AVAILABLE:INTERNAL=1\n')
            bin_dir = root / 'bin'
            bin_dir.mkdir()
            stub = '''import json, os, pathlib, sys
name = pathlib.Path(sys.argv[0]).name
root = pathlib.Path.cwd()
with open(root / 'trace.txt', 'a') as trace:
    trace.write(name + '\\n')
mode = os.environ['QA_FIXTURE_MODE']
if name == 'ip':
    print(json.dumps([{'ifname': 'lo'}] + ([{'ifname': 'eth0'}] if mode == 'network' else [])))
elif name == 'ctest' and '--show-only=json-v1' in sys.argv:
    print(json.dumps({'tests': [
        {'name': 'core case', 'command': ['/b/eiskaltdcpp-tests']},
        {'name': 'Qt case', 'command': ['/b/eiskaltdcpp-qt-tests']},
        {'name': 'torrent-backend', 'command': ['/b/eiskalt-torrent-tests']}]}))
elif name == 'timeout':
    os.execv(sys.argv[4], sys.argv[4:])
elif name == 'eiskalt-selected-recheck-dependency-tests':
    if mode == 'dependency-failure':
        sys.exit(7)
    pathlib.Path(sys.argv[sys.argv.index('--out') + 1]).write_text(
        '<testsuite><testcase name="dependency"/></testsuite>')
elif name == 'ctest':
    assert os.environ['HOME'] == str(root / 'Work/qa/linux-torrent/home')
    assert os.environ['QT_QPA_PLATFORM'] == 'offscreen'
    if mode == 'ctest-failure':
        sys.exit(8)
    pathlib.Path(sys.argv[sys.argv.index('--output-junit') + 1]).write_text(
        '<testsuite><testcase name="core case"/><testcase name="Qt case"/>'
        '<testcase name="torrent-backend"/></testsuite>')
else:
    sys.exit(99)
'''
            for path in [bin_dir / 'ip', bin_dir / 'ctest', bin_dir / 'timeout', binary]:
                path.write_text(f'#!{sys.executable}\n' + stub)
                path.chmod(0o755)
            environment = dict(os.environ, PATH=str(bin_dir) + os.pathsep + os.environ['PATH'],
                               QA_FIXTURE_MODE=mode, PYTHONDONTWRITEBYTECODE='1')
            result = subprocess.run(['bash', str(ROOT / 'tools/ci/test-linux-torrent.sh')],
                                    cwd=root, env=environment, capture_output=True, text=True,
                                    timeout=20)
            trace = (root / 'trace.txt').read_text().splitlines()
            evidence = root / 'Work/qa/linux-torrent'
            summary = json.loads((evidence / 'summary.json').read_text()) \
                if (evidence / 'summary.json').exists() else {}
            return result, trace, summary

    def test_complete_orchestration(self):
        result, trace, summary = self.run_fixture('success')
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertEqual(trace, ['ip', 'ctest', 'timeout',
                                 'eiskalt-selected-recheck-dependency-tests', 'ctest'])
        self.assertEqual(summary['dependency_cases'], 1)

    def test_external_interface_stops_before_tests(self):
        result, trace, summary = self.run_fixture('network')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(trace, ['ip'])
        self.assertEqual(summary, {})

    def test_dependency_failure_stops_ctest(self):
        result, trace, summary = self.run_fixture('dependency-failure')
        self.assertEqual(result.returncode, 7, result.stderr)
        self.assertEqual(trace.count('ctest'), 1)
        self.assertNotIn('dependency_cases', summary)

    def test_ctest_failure_cannot_create_success_summary(self):
        result, _, summary = self.run_fixture('ctest-failure')
        self.assertEqual(result.returncode, 8, result.stderr)
        self.assertNotIn('passed', summary)


if __name__ == '__main__':
    unittest.main()
