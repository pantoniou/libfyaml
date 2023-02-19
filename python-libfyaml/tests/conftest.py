import sys
import os
import glob

_tests_dir = os.path.dirname(os.path.abspath(__file__))
_src_dir   = os.path.abspath(os.path.join(_tests_dir, '..'))

# When driven by CTest, CMake sets LIBFYAML_PYTHON_BUILD_DIR to the actual
# binary directory (e.g. build-x/python-libfyaml).  Fall back to the
# conventional ../build/ heuristic so that out-of-tree manual runs still work.
_build_dir_env = os.environ.get('LIBFYAML_PYTHON_BUILD_DIR')
if _build_dir_env:
    _build_dir = os.path.abspath(_build_dir_env)
else:
    _build_dir = os.path.abspath(os.path.join(_src_dir, '../build/python-libfyaml'))

# Prefer the CMake build staging dir (has the compiled .so alongside the .py
# symlinks). Fall back to the source tree only when an in-place extension is
# present there. Wheel tests should otherwise import the installed package.
if os.path.isfile(os.path.join(_build_dir, 'libfyaml', '__init__.py')):
    sys.path.insert(0, _build_dir)
elif glob.glob(os.path.join(_src_dir, 'libfyaml', '_libfyaml*.so')) or \
        glob.glob(os.path.join(_src_dir, 'libfyaml', '_libfyaml*.pyd')):
    sys.path.insert(0, _src_dir)
