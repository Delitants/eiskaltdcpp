#!/usr/bin/env python3
"""Compile poisoned OpenSSL header fixtures against the maintained CMake rule."""
import hashlib
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
CMAKE = os.environ.get('EISKALT_TEST_CMAKE') or shutil.which('cmake')


class OpenSSLHeaders(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='eiskalt-openssl-headers-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        for name in ('selected/openssl', 'stale/openssl', 'child'):
            (self.root / name).mkdir(parents=True)
        headers = {
            'ssl.h': '#include <openssl/crypto.h>\n#include "companion.h"\n',
            'crypto.h': '#include <openssl/opensslv.h>\n',
            'opensslv.h': '#include <openssl/opensslconf.h>\n#define FIXTURE_OPENSSL_VERSION 306\n',
            'opensslconf.h': '#include <openssl/configuration.h>\n',
            'configuration.h': '#define FIXTURE_OPENSSL_CONFIGURATION 26\n',
            'companion.h': '#define FIXTURE_OPENSSL_COMPANION 19\n'}
        for name, content in headers.items():
            (self.root / 'selected/openssl' / name).write_text(content)
            (self.root / 'stale/openssl' / name).write_text(content.replace('306', '304').replace('26', '14').replace('19', '3'))
        (self.root / 'stale/unrelated.h').write_text('#define UNRELATED 7\n')
        (self.root / 'selected/unrelated.h').write_text('#define UNRELATED 99\n')
        (self.root / 'probe.cpp').write_text('''#include <openssl/ssl.h>
#include <unrelated.h>
static_assert(FIXTURE_OPENSSL_VERSION == 306, "wrong OpenSSL version");
static_assert(FIXTURE_OPENSSL_CONFIGURATION == 26, "wrong OpenSSL configuration");
static_assert(FIXTURE_OPENSSL_COMPANION == 19, "wrong OpenSSL companion");
static_assert(UNRELATED == 7, "unrelated header priority changed");
int fixture() { return 0; }
''')
        (self.root / 'child/CMakeLists.txt').write_text('''add_library(child STATIC ../probe.cpp)
target_include_directories(child PRIVATE "${CMAKE_SOURCE_DIR}/stale")
target_link_libraries(child PRIVATE wrapper)
''')
        module = (ROOT / 'cmake/OpenSSLProviderHeaders.cmake').as_posix()
        (self.root / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.10)
project(openssl_headers CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(OPENSSL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/selected")
include("''' + module + '''" OPTIONAL)
if(COMMAND eiskalt_isolate_openssl_headers)
    eiskalt_isolate_openssl_headers("${OPENSSL_INCLUDE_DIR}")
endif()
add_library(selected_ssl INTERFACE IMPORTED)
set_target_properties(selected_ssl PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}")
add_library(wrapper INTERFACE IMPORTED)
set_target_properties(wrapper PROPERTIES INTERFACE_LINK_LIBRARIES selected_ssl)
add_library(consumer STATIC probe.cpp)
target_include_directories(consumer PRIVATE "${CMAKE_SOURCE_DIR}/stale")
target_link_libraries(consumer PRIVATE wrapper)
add_subdirectory(child)
if(COMMAND eiskalt_attach_openssl_headers)
    eiskalt_attach_openssl_headers()
endif()
''')

    def call(self, args):
        env = dict(os.environ)
        env['PATH'] = str(Path(CMAKE).parent) + os.pathsep + env.get('PATH', '')
        for name in ('CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS'):
            env.pop(name, None)
        return subprocess.run(args, capture_output=True, text=True, env=env)

    def configure(self, generator='Unix Makefiles', extra=()):
        return self.call([CMAKE, '-S', str(self.root), '-B', str(self.root / 'build'),
                          '-G', generator, '-DCMAKE_BUILD_TYPE=Release', *extra])

    def build(self, target='consumer', config='Release'):
        return self.call([CMAKE, '--build', str(self.root / 'build'), '--config', config, '--target', target])

    def assert_success(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def assert_build(self, target='consumer', generator='Unix Makefiles'):
        self.assert_success(self.configure(generator))
        self.assert_success(self.build(target))

    def test_poisoned_imported_system_root(self):
        self.assert_build()

    def test_child_and_transitive_wrapper(self):
        self.assert_build('child')

    def test_ninja_header_edit_recompiles_consumer(self):
        self.assert_build(generator='Ninja')
        time.sleep(1.1)
        (self.root / 'selected/openssl/opensslv.h').write_text('#define FIXTURE_OPENSSL_VERSION 304\n')
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('wrong OpenSSL version', result.stdout + result.stderr)

    def test_make_header_edit_recompiles_consumer(self):
        self.assert_build()
        time.sleep(1.1)
        (self.root / 'selected/openssl/configuration.h').write_text('#define FIXTURE_OPENSSL_CONFIGURATION 14\n')
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('wrong OpenSSL configuration', result.stdout + result.stderr)

    def test_ninja_multi_config(self):
        self.assert_build(generator='Ninja Multi-Config')
        self.assert_success(self.build(config='Debug'))

    def test_missing_required_header_refuses_fallback(self):
        (self.root / 'selected/openssl/ssl.h').unlink()
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('ssl.h', result.stdout + result.stderr)

    def test_missing_generated_configuration_refuses_fallback(self):
        (self.root / 'selected/openssl/configuration.h').unlink()
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('configuration.h', result.stdout + result.stderr)

    def test_missing_angle_companion_refuses_fallback(self):
        (self.root / 'selected/openssl/crypto.h').unlink()
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('crypto.h', result.stdout + result.stderr)

    def test_missing_quoted_companion_refuses_fallback(self):
        (self.root / 'selected/openssl/companion.h').unlink()
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('companion.h', result.stdout + result.stderr)

    def test_build_refuses_removed_companion(self):
        self.assert_build()
        (self.root / 'selected/openssl/companion.h').unlink()
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('companion.h', result.stdout + result.stderr)

    def test_staged_header_bytes_and_origin(self):
        self.assert_build()
        staged = self.root / 'build/openssl-provider/include/openssl'
        self.assertTrue(staged.is_dir(), 'selected header staging missing')
        origins = (staged / 'origins.tsv').read_text().splitlines()
        self.assertEqual(len(origins), 6)
        for line in origins:
            name, source, digest = line.split('\t')
            expected = self.root / 'selected/openssl' / name
            self.assertEqual(Path(source).resolve(), expected.resolve())
            self.assertEqual(hashlib.sha256(expected.read_bytes()).hexdigest(), digest)
            self.assertEqual((staged / name).read_bytes(), expected.read_bytes())

    def test_removed_staged_header_is_restored(self):
        self.assert_build(generator='Ninja')
        header = self.root / 'build/openssl-provider/include/openssl/ssl.h'
        header.unlink()
        self.assert_success(self.build())
        self.assertEqual(header.read_bytes(), (self.root / 'selected/openssl/ssl.h').read_bytes())

    def test_unreferenced_source_header_removal_is_purged(self):
        extra = self.root / 'selected/openssl/extra.h'
        extra.write_text('#define EXTRA 1\n')
        self.assert_build(generator='Ninja')
        staged = self.root / 'build/openssl-provider/include/openssl/extra.h'
        self.assertTrue(staged.is_file())
        extra.unlink()
        self.assert_success(self.build())
        self.assertFalse(staged.exists())

    def test_escaping_source_symlink_refused(self):
        header = self.root / 'selected/openssl/ssl.h'
        header.unlink()
        header.symlink_to(self.root / 'stale/openssl/ssl.h')
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Escaping selected OpenSSL header', result.stdout + result.stderr)

    def test_escaping_namespace_symlink_refused(self):
        shutil.rmtree(self.root / 'selected/openssl')
        (self.root / 'selected/openssl').symlink_to(self.root / 'stale/openssl', target_is_directory=True)
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Escaping selected OpenSSL directory', result.stdout + result.stderr)

    def namespace_alias(self, matching_libraries):
        package = self.root / 'package'
        shutil.copytree(self.root / 'selected/openssl', package / 'include/openssl')
        shutil.rmtree(self.root / 'selected/openssl')
        (self.root / 'selected/openssl').symlink_to(package / 'include/openssl', target_is_directory=True)
        library_root = package if matching_libraries else self.root / 'other-package'
        (library_root / 'lib').mkdir(parents=True)
        for name in ('libssl.dylib', 'libcrypto.dylib'):
            (library_root / 'lib' / name).write_text('fixture library\n')
        cmake = self.root / 'CMakeLists.txt'
        content = cmake.read_text()
        before = 'set(OPENSSL_SSL_LIBRARY "' + str(library_root / 'lib/libssl.dylib') + '")\n'
        before += 'set(OPENSSL_CRYPTO_LIBRARY "' + str(library_root / 'lib/libcrypto.dylib') + '")\n'
        cmake.write_text(content.replace('include("', before + 'include("', 1))

    def test_package_namespace_alias_matches_both_libraries(self):
        self.namespace_alias(True)
        self.assert_build(generator='Ninja')

    def test_package_namespace_alias_wrong_libraries_refused(self):
        self.namespace_alias(False)
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Escaping selected OpenSSL directory', result.stdout + result.stderr)

    def test_package_namespace_alias_wrong_crypto_refused(self):
        self.namespace_alias(True)
        other = self.root / 'other-package/lib'
        other.mkdir(parents=True)
        (other / 'libcrypto.dylib').write_text('fixture library\n')
        cmake = self.root / 'CMakeLists.txt'
        content = cmake.read_text()
        cmake.write_text(content.replace('include("', 'set(OPENSSL_CRYPTO_LIBRARY "'
                                        + str(other / 'libcrypto.dylib') + '")\ninclude("', 1))
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('OPENSSL_CRYPTO_LIBRARY', result.stdout + result.stderr)

    def test_conflicting_provider_refused(self):
        with (self.root / 'CMakeLists.txt').open('a') as stream:
            stream.write('\neiskalt_isolate_openssl_headers("${CMAKE_SOURCE_DIR}/stale")\n')
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Conflicting selected OpenSSL header roots', result.stdout + result.stderr)

    def test_upstream_project_hook(self):
        cmake = self.root / 'CMakeLists.txt'
        content = cmake.read_text().replace('project(openssl_headers CXX)', 'project(libtorrent CXX)')
        start = content.index('set(OPENSSL_INCLUDE_DIR')
        end = content.index('add_library(selected_ssl')
        content = content[:start] + content[end:]
        content = content[:content.index('if(COMMAND eiskalt_attach_openssl_headers)')]
        cmake.write_text(content)
        modules = self.root / 'modules'
        modules.mkdir()
        (modules / 'FindOpenSSL.cmake').write_text('set(OPENSSL_FOUND TRUE)\n'
                                                  'set(OPENSSL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/selected")\n')
        self.assert_success(self.configure('Ninja', (
            '-DCMAKE_MODULE_PATH=' + str(modules),
            '-DCMAKE_PROJECT_libtorrent_INCLUDE=' + str(ROOT / 'cmake/LibtorrentOpenSSLHeaders.cmake'))))
        self.assert_success(self.build('child'))

    def test_provider_root_switch_refreshes_library_cache(self):
        recipe = self.root / 'recipe'
        for folder in ('tools/dependencies', 'dependencies/libtorrent/patches', 'cmake', 'Work/stage'):
            (recipe / folder).mkdir(parents=True)
        shutil.copy2(ROOT / 'tools/dependencies/build-libtorrent.sh', recipe / 'tools/dependencies/build-libtorrent.sh')
        for module in ('OpenSSLProviderHeaders.cmake', 'LibtorrentOpenSSLHeaders.cmake'):
            shutil.copy2(ROOT / 'cmake' / module, recipe / 'cmake' / module)
        patch = recipe / 'dependencies/libtorrent/patches/0001-selected-recheck.patch'
        patch.write_text('fixture patch\n')
        patch_sha = hashlib.sha256(patch.read_bytes()).hexdigest()
        archive = recipe / 'archive.tar.gz'
        archive.write_bytes(b'fixture archive; source already staged\n')
        (recipe / 'dependencies/libtorrent/source.json').write_text(json.dumps({
            'url': 'https://github.com/arvidn/libtorrent/releases/download/v2.1.1/libtorrent-rasterbar-2.1.1.tar.gz',
            'sha256': hashlib.sha256(archive.read_bytes()).hexdigest(), 'patch_sha256': patch_sha}))
        source_root = recipe / 'Work/stage' / ('source-' + patch_sha)
        source = source_root / 'libtorrent-rasterbar-2.1.1'
        source.mkdir(parents=True)
        (source_root / 'patched').write_text(patch_sha)
        modules = source / 'modules'
        modules.mkdir()
        (modules / 'FindOpenSSL.cmake').write_text('''find_library(OPENSSL_SSL_LIBRARY ssl PATHS "${OPENSSL_ROOT_DIR}/lib" NO_DEFAULT_PATH)
find_library(OPENSSL_CRYPTO_LIBRARY crypto PATHS "${OPENSSL_ROOT_DIR}/lib" NO_DEFAULT_PATH)
set(OPENSSL_FOUND TRUE)
''')
        (source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/modules")
project(libtorrent CXX)
add_library(consumer STATIC "''' + str(self.root / 'probe.cpp') + '''")
target_include_directories(consumer PRIVATE "''' + str(self.root / 'stale') + '''")
install(TARGETS consumer DESTINATION lib)
''')
        providers = []
        for name in ('a', 'b'):
            provider = recipe / name
            shutil.copytree(self.root / 'selected', provider / 'include')
            (provider / 'lib').mkdir()
            for library in ('libssl.dylib', 'libcrypto.dylib'):
                (provider / 'lib' / library).write_text('fixture library\n')
            providers.append(provider)
        for provider in providers:
            self.assert_success(self.call(['bash', str(recipe / 'tools/dependencies/build-libtorrent.sh'),
                '--staging-root', str(recipe / 'Work/stage'), '--prefix', str(recipe / 'Work/stage/prefix'),
                '--source-archive', str(archive), '--openssl-root', str(provider)]))
        build = recipe / 'Work/stage' / ('build-' + patch_sha + '-' + os.uname().machine)
        content = (build / 'CMakeCache.txt').read_text()
        for variable, library in (('OPENSSL_SSL_LIBRARY', 'libssl.dylib'),
                                  ('OPENSSL_CRYPTO_LIBRARY', 'libcrypto.dylib')):
            self.assertIn(variable + ':FILEPATH=' + str(providers[-1] / 'lib' / library), content)


if __name__ == '__main__':
    unittest.main(verbosity=2)
