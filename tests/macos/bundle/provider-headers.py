#!/usr/bin/env python3
"""Compile poisoned-header fixtures against the real provider-header CMake rule."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(sys.argv.pop(1)).resolve()


class ProviderHeaders(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='eiskalt-provider-headers-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        for name in ('cares', 'maxmind', 'stale'):
            (self.root / name).mkdir()
        (self.root / 'cares/ares.h').write_text('#include "ares_version.h"\n#include "ares_build.h"\n#define CARES_FIXTURE_VERSION 17\n')
        (self.root / 'cares/ares_version.h').write_text('#define CARES_COMPANION_VERSION 17\n')
        (self.root / 'cares/ares_build.h').write_text('#define CARES_BUILD_VERSION 17\n')
        (self.root / 'maxmind/maxminddb.h').write_text('#include "maxminddb_config.h"\n#define MMDB_FIXTURE_VERSION 29\n')
        (self.root / 'maxmind/maxminddb_config.h').write_text('#define MMDB_COMPANION_VERSION 29\n')
        for name in ('ares_version.h', 'ares_build.h', 'maxminddb_config.h'):
            (self.root / 'stale' / name).write_text('// stale companion must not be accepted\n')
        (self.root / 'stale/ares.h').write_text('#define CARES_FIXTURE_VERSION 3\n')
        (self.root / 'stale/maxminddb.h').write_text('#define MMDB_FIXTURE_VERSION 5\n')
        (self.root / 'cares/unrelated.h').write_text('#define UNRELATED_FIXTURE_VERSION 99\n')
        (self.root / 'stale/unrelated.h').write_text('#define UNRELATED_FIXTURE_VERSION 7\n')
        (self.root / 'probe.cpp').write_text('''#include <ares.h>
#include <maxminddb.h>
#include <unrelated.h>
static_assert(CARES_FIXTURE_VERSION == EXPECTED_CARES, "wrong c-ares provider header");
static_assert(MMDB_FIXTURE_VERSION == EXPECTED_MMDB, "wrong MaxMind provider header");
static_assert(UNRELATED_FIXTURE_VERSION == 7, "unrelated header priority changed");
int main() { return 0; }
''')
        module = (ROOT / 'cmake/ProviderHeaders.cmake').as_posix()
        (self.root / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.10)
project(provider_headers CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_library(selected_cares INTERFACE IMPORTED)
if(MODE STREQUAL "legacy")
    set_target_properties(selected_cares PROPERTIES INTERFACE_COMPILE_OPTIONS "-isystem;${CMAKE_SOURCE_DIR}/cares")
elseif(MODE STREQUAL "legacy_joined")
    set_target_properties(selected_cares PROPERTIES INTERFACE_COMPILE_OPTIONS "-isystem${CMAKE_SOURCE_DIR}/cares")
elseif(MODE STREQUAL "configuration")
    set_target_properties(selected_cares PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "$<$<CONFIG:Release>:${CMAKE_SOURCE_DIR}/cares>")
elseif(MODE STREQUAL "build_interface")
    set_target_properties(selected_cares PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/cares;${CMAKE_SOURCE_DIR}/maxmind>")
else()
    set_target_properties(selected_cares PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/cares")
endif()
if(MODE STREQUAL "explicit")
    set_target_properties(selected_cares PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/cares")
endif()
set(provider_target selected_cares)
if(MODE STREQUAL "wrapper")
    add_library(cares_static INTERFACE IMPORTED)
    set_target_properties(cares_static PROPERTIES INTERFACE_LINK_LIBRARIES selected_cares)
    set(provider_target cares_static)
endif()
add_library(other_import INTERFACE IMPORTED)
set_target_properties(other_import PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/cares;${CMAKE_SOURCE_DIR}/maxmind")
foreach(name consumer sibling)
    add_executable(${name} probe.cpp)
    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/stale")
    target_link_libraries(${name} PRIVATE ${provider_target} other_import)
endforeach()
if(NO_MAXMIND)
    set(expected_mmdb 5)
    set(maxmind_include "")
else()
    set(expected_mmdb 29)
    set(maxmind_include "${CMAKE_SOURCE_DIR}/maxmind")
endif()
target_compile_definitions(consumer PRIVATE EXPECTED_CARES=17 EXPECTED_MMDB=${expected_mmdb})
target_compile_definitions(sibling PRIVATE EXPECTED_CARES=3 EXPECTED_MMDB=5)
include("''' + module + '''" OPTIONAL)
if(COMMAND eiskalt_use_provider_headers)
    eiskalt_use_provider_headers(consumer ${provider_target} "${maxmind_include}")
endif()
''')

    def configure(self, mode='ordinary', no_maxmind=False, generator='Unix Makefiles'):
        environment = dict(os.environ)
        for name in ('CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS'):
            environment.pop(name, None)
        build = self.root / 'build'
        build.mkdir()
        result = subprocess.run([shutil.which('cmake'), '-G', generator, '-DCMAKE_BUILD_TYPE=Release', '-DMODE=' + mode,
                                 '-DNO_MAXMIND=' + ('ON' if no_maxmind else 'OFF'), str(self.root)],
                                cwd=build, capture_output=True, text=True, env=environment)
        return result

    def build(self, target='consumer'):
        return subprocess.run([shutil.which('cmake'), '--build', str(self.root / 'build'), '--target', target],
                              capture_output=True, text=True)

    def assert_builds(self, mode='ordinary', no_maxmind=False, target='consumer'):
        result = self.configure(mode, no_maxmind)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.build(target)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_provider_headers_precede_stale_search_path(self):
        self.assert_builds()

    def test_explicit_system_provider(self):
        self.assert_builds('explicit')

    def test_legacy_isystem_provider(self):
        self.assert_builds('legacy')

    def test_legacy_joined_isystem_provider(self):
        self.assert_builds('legacy_joined')

    def test_configuration_specific_provider(self):
        self.assert_builds('configuration')

    def test_transitive_provider_wrapper(self):
        self.assert_builds('wrapper')

    def test_semicolon_build_interface_provider(self):
        self.assert_builds('build_interface')

    def test_sibling_target_keeps_original_header_order(self):
        self.assert_builds(target='sibling')

    def test_unselected_maxmind_keeps_original_header_order(self):
        self.assert_builds(no_maxmind=True)

    def test_changed_cares_header_is_rejected(self):
        (self.root / 'cares/ares.h').write_text('#define CARES_FIXTURE_VERSION 3\n')
        result = self.configure()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('wrong c-ares provider header', result.stderr)

    def test_changed_maxmind_header_is_rejected(self):
        (self.root / 'maxmind/maxminddb.h').write_text('#define MMDB_FIXTURE_VERSION 5\n')
        result = self.configure()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('wrong MaxMind provider header', result.stderr)

    def test_provider_edit_triggers_reconfigure_and_rejects_changed_bytes(self):
        self.assert_builds()
        # Make can compare dependencies at whole-second timestamp granularity.
        time.sleep(1.1)
        (self.root / 'maxmind/maxminddb.h').write_text('#define MMDB_FIXTURE_VERSION 5\n')
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('wrong MaxMind provider header', result.stderr)

    def test_ninja_rebuilds_after_configuration_specific_provider_edit(self):
        self.assertIsNotNone(shutil.which('ninja'), 'Ninja is required for this regression')
        result = self.configure('configuration', generator='Ninja')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        time.sleep(1.1)
        (self.root / 'cares/ares.h').write_text('#define CARES_FIXTURE_VERSION 3\n')
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('wrong c-ares provider header', result.stdout + result.stderr)

    def test_missing_selected_cares_header_refuses_configuration(self):
        (self.root / 'cares/ares.h').unlink()
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('selected c-ares headers', result.stderr)

    def test_missing_selected_maxmind_header_refuses_configuration(self):
        (self.root / 'maxmind/maxminddb.h').unlink()
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('selected MaxMind headers', result.stderr)

    def test_missing_cares_version_refuses_ambient_companion(self):
        (self.root / 'cares/ares_version.h').unlink()
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('ares_version.h', result.stderr)

    def test_missing_cares_build_refuses_ambient_companion(self):
        (self.root / 'cares/ares_build.h').unlink()
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('ares_build.h', result.stderr)

    def test_missing_maxmind_config_refuses_ambient_companion(self):
        (self.root / 'maxmind/maxminddb_config.h').unlink()
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('maxminddb_config.h', result.stderr)

    def test_unavailable_maxmind_is_gated_in_actual_core(self):
        source = (ROOT / 'dcpp/CMakeLists.txt').read_text()
        self.assertRegex(source, r'if\s*\(MAXMINDDB_FOUND\)\s*set\s*\(_maxmind_provider_include\s+"\$\{MAXMINDDB_INCLUDE_DIR\}"\)\s*endif\s*\(\)')
        self.assertIn('eiskalt_use_provider_headers(dcpp ${EISKALT_CARES_TARGET} "${_maxmind_provider_include}")', source)


if __name__ == '__main__':
    unittest.main(verbosity=2)
