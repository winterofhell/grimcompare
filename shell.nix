# Core + tests: stdenv gcc. Adapter: clang-cl + lld-link against xwin's MSVC CRT / SDK (docs/BUILD.md).
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
  # libc.a for gdcompare-setup's static Linux release only (preset setup-linux); on the default library path it
  # would leak into every dynamic link.
  GLIBC_STATIC_LIB = "${pkgs.glibc.static}/lib";
  shellHook = ''
    export XWIN_DIR="''${XWIN_DIR:-$HOME/.local/share/xwin}"
  '';
}
