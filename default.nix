# Build libfyaml from this checkout with Nix (CMake based):
#
#   nix-build
#
{
  pkgs ? import <nixpkgs> { },
}:

pkgs.callPackage ./nix/libfyaml.nix { }
