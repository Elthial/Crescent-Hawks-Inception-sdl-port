# Version 1.1.0 release validation

Validated on 2026-10-20 from merged PR #1, with the release version and package
metadata changes. No production gameplay code changed during release preparation.

## Builds and tests

| Platform/toolchain | Configuration | Headless | SDL with copied original data |
| --- | --- | --- | --- |
| Windows x64 / MSVC | Release | 117/117 | 167/167 |
| Windows x64 / MSVC | Debug | 117/117 | 167/167 |
| Ubuntu 24.04 x86-64 / GCC 13 | Release | 117/117 | 167/167 |
| Ubuntu 24.04 x86-64 / GCC 13 | Debug | 117/117 | 167/167 |

The native Linux builds fetched the checksum-pinned SDL 3.4.16 source and linked
it statically. Nix was also checked separately: `nix flake check` passed all
148 checks, `nix build` produced a launcher reporting 1.1.0, and
`nix run .#integration-tests` passed all 167 tests on temporary asset copies.

Both training scenarios retain a 40-second timeout:

| Platform | Configuration | Normal | Delayed input |
| --- | --- | --- | --- |
| Windows | Release | 24.54 s | 30.74 s |
| Windows | Debug | 32.71 s | 39.31 s |
| Native Linux | Release | 21.05 s | 25.74 s |
| Native Linux | Debug | 31.34 s | 37.03 s |

Windows Debug still has limited timing headroom on slower or busy hosts.
Preservation layout checks passed for 24 annotated and 25 assembly segments.

## Download verification

Both archives were extracted before testing. Each contains only the executable
and `CrescentHawksInception.ini`; no original game files, saves or test fixtures
are included. Both extracted executables report version 1.1.0.

The Windows ZIP passed the minimal-package validator. The native Linux binary
has no Nix store or shared SDL dependency: its linked libraries are the system
C and math libraries. Its highest imported glibc symbol version is 2.38; the
qualified download target is Ubuntu 24.04 (glibc 2.39) or compatible newer
systems with working desktop display/audio services.

Desktop smoke tests used writable copies of original game data. Both extracted
games displayed the title and opening animation, entered a new game, accepted
southward movement, opened pause/settings menus and exited through the native
quit menu. The Linux process returned zero. A three-second WSLg audio monitor
sample contained 264,286 nonzero samples with a peak absolute value of 8,253.

WSLg display/audio services must finish starting before launching. An immediate
launch after WSL startup initially encountered an unavailable PulseAudio socket;
the same unmodified binary passed after the service became ready.

## Scope

Linux testing used Ubuntu 24.04 under WSL2/WSLg. Other distributions, aarch64,
a complete manual playthrough, physical speaker output and exact presentation
or audio fidelity are not certified by these checks.
