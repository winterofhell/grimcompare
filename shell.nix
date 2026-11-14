# Build environment. Linux core + tests use the stdenv C++ compiler (gcc).
# The Windows adapter uses clang-cl + lld-link against the MSVC CRT / Windows SDK
# that xwin splats into $XWIN_DIR (docs/BUILD.md).
{ pkgs ? import <nixpkgs> { } }:
let
  llvm = pkgs.llvmPackages_21;
in
pkgs.mkShell {
  packages = [
    pkgs.cmake
    pkgs.ninja
    pkgs.python3
    pkgs.xwin
    llvm.lld # lld-link
    llvm.llvm # llvm-rc, llvm-mt, llvm-readobj, llvm-objdump
  ];
  # Unwrapped: the nixpkgs cc-wrapper injects Linux flags that break --target=*-windows-msvc.
  # Referenced by absolute path so it does not shadow the stdenv compiler on PATH.
  CLANG_CL = "${llvm.clang-unwrapped}/bin/clang-cl";
  shellHook = ''
    export XWIN_DIR="''${XWIN_DIR:-$HOME/.local/share/xwin}"
  '';
}
