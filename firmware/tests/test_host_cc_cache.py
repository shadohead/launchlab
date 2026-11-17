#!/usr/bin/env python3
"""scripts/host_cc_cache.py must never hand back a binary built from other sources.

A logging stand-in for the real compiler shows which calls compiled; every
built program is run so a stale binary would print the wrong value.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SHIM = ROOT / 'scripts/host_cc_cache.py'
# Under the host suite `c++` is the cache itself; use the compiler behind it.
CXX = json.loads(os.environ['LAUNCHLAB_REAL_CXX'])[0] if 'LAUNCHLAB_REAL_CXX' in os.environ else shutil.which('c++')


class CompileCache(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix='launchlab-cc-cache-test-')).resolve()
        self.addCleanup(shutil.rmtree, self.work, True)
        self.log = self.work / 'compiles.log'
        real = self.work / 'real-c++'
        real.write_text(f'#!/bin/sh\necho "$*" >> {self.log}\nexec {CXX} "$@"\n')
        real.chmod(0o755)
        self.suite_tmp = self.work / 'run'
        self.env = dict(os.environ, LAUNCHLAB_REAL_CXX=json.dumps([str(real)]), LAUNCHLAB_CXX_ID='test',
                        LAUNCHLAB_CC_CACHE=str(self.work / 'cache'), LAUNCHLAB_SUITE_TMP=str(self.suite_tmp))
        self.source = self.work / 'src'
        self.source.mkdir()
        self.header('#pragma once\nconstexpr int VALUE = 1;\n')
        (self.source / 'main.cpp').write_text('#include <cstdio>\n#include "value.h"\n'
                                              'int main(){std::printf("%d\\n", VALUE);}\n')

    def header(self, text):
        (self.source / 'value.h').write_text(text)

    def build(self, *flags, source=None, output='prog'):
        self.log.write_text('')
        result = subprocess.run([sys.executable, str(SHIM), '-std=c++17', '-O1', *flags,
                                 str(source or self.source / 'main.cpp'), '-o', str(self.work / output)],
                                env=self.env, capture_output=True, text=True)
        compiled = [line for line in self.log.read_text().splitlines() if ' -E ' not in f' {line} ']
        return result, compiled

    def run_program(self, output='prog'):
        return subprocess.run([str(self.work / output)], capture_output=True, text=True, check=True).stdout.strip()

    def test_identical_compile_is_reused(self):
        result, compiled = self.build()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(compiled), 1)
        (self.work / 'prog').unlink()
        result, compiled = self.build()
        self.assertEqual((result.returncode, compiled), (0, []))
        self.assertEqual(self.run_program(), '1')

    def test_changed_header_recompiles(self):
        self.build()
        self.header('#pragma once\nconstexpr int VALUE = 2;\n')
        result, compiled = self.build()
        self.assertEqual(len(compiled), 1)
        self.assertEqual(self.run_program(), '2')

    def test_comment_only_change_is_reused(self):
        self.build()
        self.header('#pragma once\nconstexpr int VALUE = 1; // explained\n')
        result, compiled = self.build()
        self.assertEqual(compiled, [])
        self.assertEqual(self.run_program(), '1')

    def test_flags_are_part_of_the_key(self):
        (self.source / 'main.cpp').write_text('#include <cstdio>\n#ifndef EXTRA\n#define EXTRA 0\n#endif\n'
                                              'int main(){std::printf("%d\\n", EXTRA);}\n')
        self.build()
        result, compiled = self.build('-DEXTRA=5')
        self.assertEqual(len(compiled), 1)
        self.assertEqual(self.run_program(), '5')
        result, compiled = self.build('-O2', '-DEXTRA=5')
        self.assertEqual(len(compiled), 1)

    def test_failed_compile_is_not_cached(self):
        self.header('#pragma once\nconstexpr int VALUE = ;\n')
        for _ in range(2):
            result, compiled = self.build()
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(len(compiled), 1)

    def test_warnings_are_replayed_on_a_hit(self):
        self.header('#pragma once\nconstexpr int VALUE = 1;\n#warning cached-warning\n')
        first, _ = self.build()
        second, compiled = self.build()
        self.assertEqual(compiled, [])
        self.assertIn('cached-warning', first.stderr)
        self.assertIn('cached-warning', second.stderr)

    def test_random_temporary_directories_share_entries(self):
        for name in ('tmpaaaa1111', 'tmpbbbb2222'):
            directory = self.suite_tmp / 'tmp' / 'check_example' / name
            directory.mkdir(parents=True)
            shutil.copy(self.source / 'value.h', directory)
            shutil.copy(self.source / 'main.cpp', directory)
            result, compiled = self.build(f'-I{directory}', source=directory / 'main.cpp')
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(compiled, [])
        self.assertEqual(self.run_program(), '1')

    def test_unmodelled_invocations_go_straight_to_the_compiler(self):
        for flags in (['-E'], ['-MD'], ['-x', 'c++'], ['-Wl,-v']):
            with self.subTest(flags=flags):
                for _ in range(2):  # one real call each time: no preprocessing pass, no reuse
                    self.build(*flags)
                    self.assertEqual(len(self.log.read_text().splitlines()), 1, flags)
        self.log.write_text('')
        object_file = self.work / 'main.o'
        subprocess.run([sys.executable, str(SHIM), '-c', str(self.source / 'main.cpp'), '-o', str(object_file)],
                       env=self.env, check=True)
        subprocess.run([sys.executable, str(SHIM), str(object_file), '-o', str(self.work / 'linked')],
                       env=self.env, check=True)
        self.assertEqual(self.run_program('linked'), '1')


if __name__ == '__main__':
    unittest.main(verbosity=1, argv=[sys.argv[0]])
