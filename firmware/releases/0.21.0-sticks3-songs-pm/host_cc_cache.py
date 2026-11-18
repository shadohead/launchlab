#!/usr/bin/env python3
"""Compile cache for the firmware host suite (the idea of ccache's preprocessor mode).

scripts/firmware_tests.py puts this in front of the compiler as `c++` and $CXX.
A compile whose preprocessed sources, flags, compiler and environment match an
earlier one gets that compile's output back instead of recompiling; the test
binaries always run fresh. Anything it does not recognise goes straight to the
real compiler.

The suite's temporary directories have random names, so they are replaced by a
placeholder in the key. A cached binary can therefore name an earlier run's
temporary directory in its debug info or __FILE__ strings; nothing else differs.

Environment (set by firmware_tests.py):
  LAUNCHLAB_REAL_CXX    JSON list: the real compiler command
  LAUNCHLAB_CXX_ID      compiler identity (its --version output)
  LAUNCHLAB_CC_CACHE    cache directory
  LAUNCHLAB_SUITE_TMP   the run's temporary root, replaced in keys
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

SOURCE = re.compile(r'.*\.(c|cc|cpp|cxx|c\+\+)$')
# Options whose value is the next argument.
WITH_VALUE = {'-o', '-I', '-D', '-U', '-include', '-isystem', '-iquote', '-idirafter', '-L', '-l',
              '-arch', '-target', '-isysroot', '-Xlinker', '-framework'}
# Inputs that only the link step reads; their contents join the key.
LINK_INPUT = re.compile(r'.*\.(o|a|so|dylib)$')
# Modes and inputs this cache does not model.
UNCACHED = re.compile(r'^(-E|-S|-M.*|-fsyntax-only|-x.*|-|@.*|-save-temps.*|--?help|-v|-###|-fprofile.*|'
                      r'-fcoverage.*|-ftime-trace.*|-Wl,.*|-Xclang)$')
ENVIRONMENT = ('CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH', 'SDKROOT',
               'MACOSX_DEPLOYMENT_TARGET', 'DEVELOPER_DIR')


def real_compile(real, args):
    os.execvp(real[0], real + args)


def parse(args):
    """(sources, output, link inputs, flags for preprocessing) or None when not cacheable."""
    sources, inputs, preprocess = [], [], []
    output = None
    index = 0
    while index < len(args):
        arg = args[index]
        if UNCACHED.match(arg):
            return None
        if arg in WITH_VALUE:
            if index + 1 >= len(args):
                return None
            value = args[index + 1]
            if arg == '-o':
                output = value
            elif arg in ('-L', '-l', '-Xlinker', '-framework'):
                pass
            else:
                preprocess += [arg, value]
            index += 2
            continue
        if arg.startswith('-o') and len(arg) > 2:
            output = arg[2:]
        elif arg.startswith(('-l', '-L')):
            pass
        elif arg.startswith('-'):
            if arg != '-c':
                preprocess.append(arg)
        elif SOURCE.match(arg):
            sources.append(arg)
        elif LINK_INPUT.match(arg):
            inputs.append(arg)
        else:
            return None
        index += 1
    if not sources or not output:
        return None
    return sources, output, inputs, preprocess


def main():
    args = sys.argv[1:]
    real = json.loads(os.environ['LAUNCHLAB_REAL_CXX'])
    cache = os.environ.get('LAUNCHLAB_CC_CACHE')
    parsed = parse(args) if cache else None
    if parsed is None:
        real_compile(real, args)
    sources, output, inputs, preprocess = parsed
    temporary = os.environ.get('LAUNCHLAB_SUITE_TMP', '')

    def normal(text):
        # <run tmp>/tmp/<suite>/<random dir> -> @/<suite>/@
        if temporary:
            text = re.sub(re.escape(temporary) + r'/tmp/([^/\s"]+)/[^/\s"]+', r'@/\1/@', text)
            text = text.replace(temporary, '@')
        return text

    key = hashlib.sha256()
    key.update(json.dumps([real, os.environ.get('LAUNCHLAB_CXX_ID', ''), os.getcwd(),
                           [os.environ.get(name) for name in ENVIRONMENT],
                           [normal(arg) for arg in args if arg != output]]).encode())
    for source in sources:
        result = subprocess.run(real + preprocess + ['-E', source], capture_output=True)
        if result.returncode != 0:
            real_compile(real, args)
        key.update(normal(result.stdout.decode(errors='surrogateescape')).encode(errors='surrogateescape'))
    for path in inputs:
        key.update(hashlib.sha256(Path(path).read_bytes()).digest())
    digest = key.hexdigest()
    entry = Path(cache) / digest[:2] / digest
    if entry.with_suffix('.out').exists():
        data = entry.with_suffix('.out').read_bytes()
        sys.stderr.buffer.write(entry.with_suffix('.err').read_bytes())
        partial = Path(f'{output}.cache-{os.getpid()}')
        partial.write_bytes(data)
        partial.chmod(entry.with_suffix('.out').stat().st_mode & 0o777)
        os.replace(partial, output)
        os.utime(entry.with_suffix('.out'))  # recently used, for pruning
        return 0
    result = subprocess.run(real + args, stderr=subprocess.PIPE)
    sys.stderr.buffer.write(result.stderr)
    if result.returncode == 0 and Path(output).is_file():
        entry.parent.mkdir(parents=True, exist_ok=True)
        for suffix, data in (('.err', result.stderr), ('.out', Path(output).read_bytes())):
            partial = entry.with_suffix(f'{suffix}.{os.getpid()}')
            partial.write_bytes(data)
            if suffix == '.out':
                partial.chmod(Path(output).stat().st_mode & 0o777)
            os.replace(partial, entry.with_suffix(suffix))  # .out last: it marks a complete entry
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
