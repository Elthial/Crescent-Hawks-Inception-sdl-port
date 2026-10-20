# Changelog

All notable preservation releases are documented here. This project uses
semantic release numbers for its own reconstructed source and host layer; they
do not identify an original DOS release.

## 1.1.0 - 2026-10-20

- Added Linux x86-64 downloads alongside the Windows x64 runtime package.
- Added pinned Nix builds, a development shell, a launcher that copies original
  game data into a writable play directory, and Linux CI.
- Stabilized automated SDL input and training progress observation while
  retaining the 40-second training deadlines.
- Made the CMake runtime package use the platform's executable filename and
  made the Nix package version follow `VERSION`.
- Kept original game data out of both packages; no production gameplay logic
  changed.

## 1.0.0 - 2026-10-02

- Established the first standalone preservation snapshot.
- Added the C17 reconstruction, self-contained evidence and verification tests.
- Added the SDL3 replacement for DOS, BIOS and hardware boundaries.
- Added a statically linked Windows runtime package that requires user-supplied
  original game data.
- Declared Windows x64 as the only validated v1.0 host platform.
