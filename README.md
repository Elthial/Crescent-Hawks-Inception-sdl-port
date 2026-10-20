<h1 align="center">
  <img src="docs/images/title-banner.png" alt="BattleTech: The Crescent Hawk's Inception" width="560">
</h1>

<p align="center"><em>A readable C17 reconstruction of the 1988 DOS game.</em></p>

This repository is a C17 reconstruction of the 1988 DOS game. Its purpose is
preservation: represent the executable's procedures, data, control flow and
BattleTech rules as readable C while retaining a direct route back to the
assembly evidence.

This repository starts at the consolidated v1.0 preservation snapshot. The
monorepo's development commit history is intentionally not imported.

This is not a redesign. DOS, BIOS and hardware operations are redirected at
the lowest practical boundary to SDL3 and portable host services; game logic
remains in the reconstructed procedure chain.

## Current status

Version 1.1 provides Windows x64 and Linux x86-64 downloads. The Linux binary
targets Ubuntu 24.04 or compatible systems with glibc 2.39 or newer and an
X11 or Wayland desktop. Other Linux distributions and aarch64 are not
release-qualified.
See [version 1.1 validation](docs/RELEASE_1_1_VALIDATION.md) for build, test and
download smoke-test results.

- The reconstructed source is divided between original logic in
  `src/Original` and platform replacements in `src/SDL`.
- The real entry point reaches the original `Setup_Game` procedure; production
  builds do not substitute a test bootstrap or replacement game loop.
- The test suites contain 117 asset-independent headless tests and 148 SDL
  tests without private game data. Supplying a legal original installation
  enables 19 additional local asset-integration tests (167 total).
- Original title, map, exploration, menus, services, training, combat,
  save/load and shutdown paths have connected coverage.
- A complete manual playthrough and exact presentation/audio fidelity remain
  uncertified. See [`docs/RUNTIME_STATUS.md`](docs/RUNTIME_STATUS.md).

<p align="center">
  <img src="docs/images/gameplay.png" alt="The reconstructed game displaying a Wasp BattleMech status screen" width="853">
</p>

## Quick start

### Release downloads

Download the Windows x64 ZIP or Linux x86-64 tarball from
[Releases](https://github.com/Elthial/Crescent-Hawks-Inception-sdl-port/releases).
Extract the two runtime files into a writable **copy** of your legally obtained
original game installation. Run `CrescentHawksInception.exe` on Windows or
`./CrescentHawksInception` on Linux. Saves are written to that copied directory.
Linux filenames are case-sensitive; retain the original uppercase data filenames.

Both downloads statically link SDL3 and contain no original game data.
The Linux download does not require Nix. It uses system display/audio drivers;
Ubuntu desktop normally supplies these, while WSL requires WSLg.
`CHI_ASSET_DIRECTORY` can instead select another writable copy of the data.

### Linux with Nix

Requires Nix with flakes enabled and a legally obtained original game installation.
Set `CHI_ORIGINAL_ASSET_DIRECTORY` to that installation, then run:

```sh
nix run .
```

On first launch, the launcher copies game data into
`${XDG_DATA_HOME:-$HOME/.local/share}/crescent-hawks-inception`. Saves remain
there across rebuilds. `CHI_ASSET_DIRECTORY` overrides this writable play
directory; do not point it at the originals. No game data is bundled.

Use `nix build` to build, `nix develop` for a CMake/SDL3 shell, and
`nix flake check` for asset-independent tests. `nix run .#integration-tests`
requires `CHI_ORIGINAL_ASSET_DIRECTORY` and tests a temporary copy of the data.
Pass CTest arguments after `--`; set `CHI_KEEP_TEST_DIRECTORY=1` to keep the
build and copied data for debugging.

Dependencies are pinned; CMake dependency fetching is disabled.
See [Linux test status](docs/LINUX_VALIDATION.md) for validation limits.

### Windows

Requirements are Windows, PowerShell, Visual Studio C/C++ build tools and a
legally obtained original game installation. CMake/Ninja are taken from Visual
Studio; SDL3 is fetched at its pinned checksum unless supplied explicitly.

Run the asset-independent Release build and tests:

```powershell
./Build.ps1 -Headless -Configuration Release
```

For local play, put the original data files in `assets/original-game` and run:

```powershell
./Run-Inception.ps1
```

Or use an installation elsewhere:

```powershell
./Run-Inception.ps1 -OriginalAssetDirectory ../Chinception
```

The direct launcher allows the game to update saves in the selected directory.
Use `-Isolated` to play from a private copy under the ignored build directory.
The old `Run.ps1` name remains as a compatibility wrapper.

## Minimal runtime package

Create a distributable runtime shell with:

```powershell
./Build.ps1 -Configuration Release -Package
```

`build-release/package` contains exactly:

```text
CrescentHawksInception.exe
CrescentHawksInception.ini
```

SDL3 is linked statically. The package contains no copyrighted game data.
`build-release/CrescentHawksInception-1.1.0-SHA256SUMS.txt` records the SHA-256
of both package files and is intentionally kept beside the package directory.
With `asset_directory=.`, both files may be copied into an original game
installation. A relative or absolute path may instead keep program and data
separate. `CHI_ASSET_DIRECTORY` overrides the file for launchers and tests.

For the Linux download, build on Ubuntu 24.04 with the SDL Linux build
dependencies installed:

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DCHI_WARNINGS_AS_ERRORS=OFF \
  -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON
cmake --build build-release
ctest --test-dir build-release --output-on-failure --parallel 1
cmake --build build-release --target inception-package
```

This uses the checksum-pinned SDL3 source and links it statically. The Linux
package contains `CrescentHawksInception` and `CrescentHawksInception.ini`.

## Repository map

| Path | Purpose |
| --- | --- |
| `src/Original` | Readable reconstruction of executable procedures and data |
| `src/SDL` | SDL3 and host implementations below DOS/hardware boundaries |
| `src/Original/*.h`, `src/SDL/*.h` | Interfaces owned by their respective source domains |
| `tests` | Behavioral, integration and preservation checks |
| `evidence` | Self-contained annotations, assembly, metadata and research |
| `docs` | Current policy, runtime status and focused conversion records |
| `tools` | Repository and package validation scripts |
| `assets/original-game` | Ignored placeholder for user-supplied original data |

The governing rules are in
[`docs/PRESERVATION_PLAN.md`](docs/PRESERVATION_PLAN.md), and directory
ownership is described in [`docs/SOURCE_LAYOUT.md`](docs/SOURCE_LAYOUT.md).
The supported DOS executable hashes are recorded in
[`docs/SUPPORTED_VERSIONS.md`](docs/SUPPORTED_VERSIONS.md).

Contributions must follow [`CONTRIBUTING.md`](CONTRIBUTING.md), particularly the
original-bug and SDL-boundary rules. Release changes are in
[`CHANGELOG.md`](CHANGELOG.md).

## Rights and licensing

Project-authored code and excluded reconstructed or third-party material are
described in [`LICENSE`](LICENSE), [`LICENSES/README.md`](LICENSES/README.md),
[`NOTICE.md`](NOTICE.md), and [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
The original executable, game assets and private saves are not included.

This is an independent preservation and research project. It is not affiliated
with or endorsed by the game's publishers or the owners of BattleTech.
