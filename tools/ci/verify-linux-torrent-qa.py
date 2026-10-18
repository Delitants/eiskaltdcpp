#!/usr/bin/env python3
"""Refuse incomplete platform test evidence; preserve skips in the summary."""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET

SUITES = {'eiskaltdcpp-tests': 'core', 'eiskaltdcpp-qt-tests': 'qt',
          'eiskalt-torrent-tests': 'torrent'}


def configuration(text):
    values = dict(line.split('=', 1) for line in text.splitlines()
                  if '=' in line and not line.startswith(('#', '//')))
    for key, expected in {'USE_TORRENT:BOOL': 'ON', 'BUILD_TESTS:BOOL': 'ON',
                          'EISKALT_REQUIRE_SELECTED_RECHECK:BOOL': 'ON',
                          'EISKALT_SELECTED_RECHECK_AVAILABLE:INTERNAL': '1'}.items():
        if values.get(key) != expected:
            raise ValueError(f'Required configuration missing: {key}={expected}')


def inventory(data):
    found = {}
    for test in data['tests']:
        command = test.get('command', [])
        if not command:
            raise ValueError(f'Test has no executable: {test["name"]}')
        suite = SUITES.get(Path(command[0]).name, 'other')
        if test['name'] in found:
            raise ValueError(f'Duplicate test name: {test["name"]}')
        found[test['name']] = suite
    if not set(SUITES.values()).issubset(found.values()):
        raise ValueError('Core, Qt and Torrent suites must all be registered')
    return found


def results(registered, text):
    cases = ET.fromstring(text).findall('.//testcase')
    names = [case.attrib['name'] for case in cases]
    if len(set(names)) != len(names) or set(names) != set(registered):
        raise ValueError('JUnit results do not match the registered test inventory')
    passed = dict.fromkeys((*SUITES.values(), 'other'), 0)
    skipped = []
    for case in cases:
        name = case.attrib['name']
        if case.find('failure') is not None or case.find('error') is not None:
            raise ValueError(f'Test failed: {name}')
        if case.find('skipped') is not None or case.get('status') in ('notrun', 'disabled'):
            skipped.append(name)
        else:
            passed[registered[name]] += 1
    if not all(passed[suite] for suite in SUITES.values()) or any(
            registered[name] == 'torrent' for name in skipped):
        raise ValueError('A required suite was skipped')
    return {'passed': passed, 'skipped': skipped}


def dependency_results(text):
    cases = ET.fromstring(text).findall('.//testcase')
    if not cases or any(case.find(tag) is not None for case in cases
                        for tag in ('failure', 'error', 'skipped')):
        raise ValueError('Dependency tests must execute without failures or skips')
    return len(cases)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('evidence', type=Path)
    parser.add_argument('--results', action='store_true')
    args = parser.parse_args()
    configuration((args.build / 'CMakeCache.txt').read_text())
    registered = inventory(json.loads((args.evidence / 'inventory.json').read_text()))
    summary = {'registered': registered}
    if args.results:
        summary.update(results(registered, (args.evidence / 'ctest.xml').read_text()))
        summary['dependency_cases'] = dependency_results(
            (args.evidence / 'dependency.xml').read_text())
    (args.evidence / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
