# Automated releases

The **Release Windows and Linux** GitHub Actions workflow attaches runtime
downloads when a GitHub release is published. A failed build or test prevents
the attachment job from running.

## Publish a new version

1. Update `VERSION` and `CHANGELOG.md`, perform the local original-data and
   desktop checks, and commit the release changes. Nix reads `VERSION`.
2. Create a tag named `v<VERSION>` at that commit, for example `v1.2.0`.
3. Publish a GitHub release for that tag. The release workflow starts
   automatically and builds the tagged commit, not a moving branch.
4. Once Windows, Linux and Nix pass, the workflow attaches:
   - `CrescentHawksInception-<VERSION>-windows-x64.zip`
   - `CrescentHawksInception-<VERSION>-linux-x86_64.tar.gz`
   - `CrescentHawksInception-<VERSION>-SHA256SUMS.txt`

The release page is visible while the jobs run; downloads appear after all
checks succeed. Both stable releases and prereleases use the `published` event.
Draft releases do not start it. Create/publish releases through GitHub's UI,
the CLI or a personal token: releases created with another workflow's
`GITHUB_TOKEN` do not trigger downstream release workflows.

## Checks

Each native platform builds and tests both Release and Debug, with at least
117 headless and 148 asset-independent SDL tests per configuration. It validates
the preservation evidence, fetches checksum-pinned SDL3 and links it statically,
builds the two-file runtime package, extracts the archive, and checks the
executable's version. Linux is built on Ubuntu 24.04; its runtime dependency
check rejects missing libraries and shared SDL/Nix dependencies.

A separate Nix job runs the pinned flake checks and builds/version-checks its
launcher. The final bundle must contain both native archives; checksums are
generated and verified before the upload job gets write permission.

GitHub-hosted runners have no original game data. The 19 original-data tests,
desktop interaction, physical audio and full-playthrough checks are **not**
automated by this workflow. Keep those local, using writable copies of the
original installation. No game data or private saves are uploaded.

## Dry runs and retries

In Actions, choose **Release Windows and Linux → Run workflow**. Leave
`release_tag` empty and `attach_assets` unchecked to test the selected branch
or commit without changing any release. The resulting `release-bundle` Actions
artifact contains both downloads and their checksum file.

To attach downloads to an existing published release, supply its tag and check
`attach_assets`. The tag must match `VERSION` and include this workflow and its
scripts. Older releases, such as v1.1.0, retain their manually uploaded assets.

If any build/test fails, fix the cause before retrying. If only an upload fails,
use **Re-run failed jobs** to retain the already tested artifacts. The uploader
skips existing assets only when their SHA-256 matches, refuses to overwrite a
different asset, and checks that the release tag still names the tested commit.
Uploads are separate GitHub API calls; a network failure can leave a partial
bundle, which can be completed by retrying the attachment job.

For local reproduction, install PowerShell 7, Python 3, CMake/Ninja and the
platform compiler/SDL build dependencies, then run:

```powershell
./tools/Build-ReleaseArtifact.ps1
```

Use a fresh `-BuildRoot` for another packaging run. Test reports and archives are
written under `out/release-ci` by default. The workflow lists the Ubuntu packages;
Windows uses the Visual Studio C/C++ tools.
