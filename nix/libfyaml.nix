# Nix derivation for libfyaml, built with CMake from this source tree.
#
#   nix-build                       # build and test (see ../default.nix)
#   nix-build -A libfyaml.dev       # headers, pkg-config and CMake files
#
# The tests run in the check phase. They need no network (the YAML and JSON
# test suites are not fetched), so the build works in the Nix sandbox.
{
  lib,
  stdenv,
  cmake,
  pkg-config,
  libyaml,
  check,
  jq,
  bash,
}:

stdenv.mkDerivation {
  pname = "libfyaml";
  # the tree carries its version in .tarball-version (there is no .git here)
  version = lib.strings.trim (builtins.readFile ../.tarball-version);

  # this checkout, without build directories and editor files; use
  # overrideAttrs to build another source
  src = lib.cleanSource ../.;

  outputs = [
    "out"
    "bin"
    "dev"
    "man"
  ];

  nativeBuildInputs = [
    cmake
    pkg-config
  ];

  buildInputs = [ libyaml ];

  nativeCheckInputs = [
    check
    jq
    bash
  ];

  cmakeFlags = [
    (lib.cmakeBool "BUILD_TESTING" true)
    (lib.cmakeBool "ENABLE_NETWORK" false)
    (lib.cmakeBool "ENABLE_LIBCLANG" false)
    (lib.cmakeBool "ENABLE_PYTHON_BINDINGS" false)
  ];

  doCheck = true;

  meta = {
    description = "Fully feature complete YAML parser and emitter, supporting the latest YAML spec and passing the full YAML testsuite";
    homepage = "https://github.com/pantoniou/libfyaml";
    license = lib.licenses.mit;
    mainProgram = "fy-tool";
    platforms = lib.platforms.all;
  };
}
