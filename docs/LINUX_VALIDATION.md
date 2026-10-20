# Linux test status

Validation on 2026-10-20 used x86_64 Ubuntu 24.04 under WSL2, Nix 2.28.5,
the locked nixpkgs input and SDL 3.4.16. The Nix build, launcher version check
and all 148 asset-independent checks passed.

| Platform | Configuration | Headless tests | SDL tests with copied original data |
| --- | --- | --- | --- |
| Linux / Nix | Release | 117/117 | 167/167 |
| Linux / Nix | Debug | 117/117 | 167/167 |
| Windows / MSVC | Release | 117/117 | 167/167 |
| Windows / MSVC | Debug | 117/117 | 167/167 |

Both training scenarios use a 40-second CTest timeout. The full-suite timings
were:

| Platform | Configuration | Normal training | Delayed-input training |
| --- | --- | --- | --- |
| Linux / Nix | Release | 20.69 s | 25.68 s |
| Linux / Nix | Debug | 20.71 s | 26.04 s |
| Windows / MSVC | Release | 24.45 s | 30.77 s |
| Windows / MSVC | Debug | 32.96 s | 39.19 s |

Both Linux training scenarios also passed three consecutive focused runs.
The Windows Debug delayed-input case passed three further runs with the
40-second limit active (39.43, 38.77 and 38.97 seconds). It has little headroom;
these measurements do not guarantee the same timing on slower hosts.

The Windows preservation-layout and minimal runtime-package checks passed.
All 89 original installation files retained their hashes. A WSLg smoke test
also exercised rendering, new-game entry, movement, menus, audio-stream output
and native quit. aarch64-linux, a complete manual playthrough and exact
display/audio fidelity remain unvalidated.

## Automated input fixes

Linux testing exposed timing failures in tests that simulate keyboard input.
The test changes pace input, retry discarded keys, and check game-loop progress
rather than temporary screen layouts. No production gameplay code was changed.
It is not established whether these failures also occur on other platforms.

The initial full Linux suite timed out in the normal training scenario.
Its operator sampled the modulo-four world-update phase only after the input
quiet interval. Whole counter wraps between samples were invisible, so the
test could substantially undercount completed updates and miss its deadline.
Progress is now observed on every main-thread callback, independently of
whether another input batch is due. Input pacing, the late-return assertion
and the mission outcome checks are retained. The original 40-second timeout
is restored and also applied to the delayed-input training scenario.

Local-data tests run serially because they share writable data and have timing
deadlines. See [README.md](../README.md#linux-with-nix) for test commands.
