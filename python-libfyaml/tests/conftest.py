import sys
import os

_tests_dir = os.path.dirname(os.path.abspath(__file__))
_src_dir   = os.path.abspath(os.path.join(_tests_dir, '..'))
_build_dir = os.path.abspath(os.path.join(_src_dir, '../build/python-libfyaml'))

# Prefer the CMake build staging dir (has the compiled .so alongside the .py
# symlinks). Fall back to the source tree when no build dir exists (e.g. after
# a plain 'pip install -e .' without running cmake).
if os.path.isfile(os.path.join(_build_dir, 'libfyaml', '__init__.py')):
    sys.path.insert(0, _build_dir)
else:
    sys.path.insert(0, _src_dir)
