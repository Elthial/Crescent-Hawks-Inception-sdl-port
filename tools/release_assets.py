"""Build, verify and attach the two runtime-only release archives."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import urllib.parse
import urllib.request
import zipfile


def validate_version(version):
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?", version):
        raise ValueError("Expected a semantic version, for example 1.2.0")
    return version


def archive_names(version):
    validate_version(version)
    prefix = f"CrescentHawksInception-{version}"
    return [f"{prefix}-windows-x64.zip", f"{prefix}-linux-x86_64.tar.gz"]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_runtime(directory, platform, version):
    exe = "CrescentHawksInception" + (".exe" if platform == "windows" else "")
    expected = {exe, "CrescentHawksInception.ini"}
    if {p.name for p in directory.iterdir()} != expected:
        raise ValueError("Runtime package must contain only executable and configuration")
    if any(p.is_symlink() or not p.is_file() for p in directory.iterdir()):
        raise ValueError("Runtime package entries must be ordinary files")
    if not re.search(r"(?m)^asset_directory=\.$", (directory / "CrescentHawksInception.ini").read_text()):
        raise ValueError("Missing default asset-directory configuration")
    result = subprocess.run([str(directory / exe), "--version"], check=True,
                            capture_output=True, text=True, timeout=15)
    if result.stdout.strip() != f"CrescentHawksInception {version}":
        raise ValueError("Packaged executable version does not match VERSION")
    if platform == "linux":
        linked = subprocess.run(["ldd", str(directory / exe)], check=True,
                                capture_output=True, text=True, timeout=15).stdout
        if any(marker in linked for marker in ("not found", "/nix/store/", "libSDL")):
            raise ValueError(f"Linux archive is not standalone from SDL/Nix:\n{linked}")
        print(linked)
    return sorted(expected)


def package(directory, output, platform, version):
    validate_version(version)
    directory, output = directory.resolve(), output.resolve()
    names = verify_runtime(directory, platform, version)
    output.mkdir(parents=True, exist_ok=True)
    archive = output / archive_names(version)[0 if platform == "windows" else 1]
    if archive.exists():
        raise ValueError(f"Archive already exists: {archive}")
    if platform == "windows":
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
            for name in names:
                bundle.write(directory / name, name)
    else:
        with tarfile.open(archive, "w:gz") as bundle:
            for name in names:
                bundle.add(directory / name, arcname=name, recursive=False)
    # Test the actual extracted distribution, not only the build-tree program.
    with tempfile.TemporaryDirectory(prefix="verify-", dir=output) as temporary:
        extracted = Path(temporary).resolve()
        if extracted.parent != output:
            raise ValueError("Temporary extraction escaped output directory")
        if platform == "windows":
            with zipfile.ZipFile(archive) as bundle:
                if sorted(bundle.namelist()) != names:
                    raise ValueError("Unexpected ZIP entries")
                for name in names:
                    (extracted / name).write_bytes(bundle.read(name))
        else:
            with tarfile.open(archive) as bundle:
                if sorted(bundle.getnames()) != names:
                    raise ValueError("Unexpected tar entries")
                for member in bundle.getmembers():
                    if not member.isfile():
                        raise ValueError("Unexpected non-file tar entry")
                    with bundle.extractfile(member) as contents:
                        (extracted / member.name).write_bytes(contents.read())
                    (extracted / member.name).chmod(member.mode & 0o777)
        verify_runtime(extracted, platform, version)
    print(f"Verified {archive.name}: {digest(archive)}")


def make_manifest(directory, version):
    names = archive_names(version)
    manifest = directory / f"CrescentHawksInception-{version}-SHA256SUMS.txt"
    actual = {p.name for p in directory.iterdir()}
    if actual - {manifest.name} != set(names):
        raise ValueError("Expected exactly one Windows and one Linux archive")
    if manifest.is_symlink():
        raise ValueError("Checksum manifest must not be a symlink")
    for name in names:
        if (directory / name).is_symlink() or not (directory / name).is_file():
            raise ValueError("Release archives must be ordinary files")
    manifest.write_text("".join(f"{digest(directory / name)}  {name}\n" for name in names),
                        encoding="ascii")
    return [directory / name for name in names] + [manifest]


class GitHub:
    def __init__(self, token):
        self.token = token

    def request(self, path, data=None, content_type="application/json"):
        request = urllib.request.Request(
            "https://api.github.com" + path, data=data,
            headers={"Authorization": f"Bearer {self.token}",
                     "Accept": "application/vnd.github+json",
                     "X-GitHub-Api-Version": "2022-11-28",
                     "Content-Type": content_type})
        with urllib.request.urlopen(request, timeout=120) as response:
            return json.load(response)

    def upload(self, repository, release_id, file):
        url = (f"https://uploads.github.com/repos/{repository}/releases/{release_id}/assets"
               f"?name={urllib.parse.quote(file.name, safe='')}")
        request = urllib.request.Request(url, data=file.read_bytes(),
                                         headers={"Authorization": f"Bearer {self.token}",
                                                  "Content-Type": "application/octet-stream"})
        with urllib.request.urlopen(request, timeout=120) as response:
            return json.load(response)


def publish(directory, version, repository, tag, commit, api):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Invalid repository name")
    if tag != f"v{validate_version(version)}":
        raise ValueError("Release tag must match VERSION")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Expected the exact tested commit SHA")
    files = make_manifest(directory, version)
    prefix = f"/repos/{repository}"
    encoded_tag = urllib.parse.quote(tag, safe="")
    resolved = api.request(f"{prefix}/commits/{encoded_tag}")
    if resolved["sha"] != commit:
        raise ValueError("Release tag moved after the build")
    release = api.request(f"{prefix}/releases/tags/{encoded_tag}")
    if release["tag_name"] != tag or release.get("draft"):
        raise ValueError("Expected an existing published release for the tested tag")
    existing = {asset["name"]: asset for asset in release["assets"]}
    pending = []
    # Preflight the entire bundle before any upload. Never clobber a release.
    for file in files:
        expected = f"sha256:{digest(file)}"
        if file.name in existing:
            asset = existing[file.name]
            if asset.get("digest") != expected or asset.get("state") != "uploaded":
                raise ValueError(f"Existing release asset differs: {file.name}")
        else:
            pending.append((file, expected))
    for file, expected in pending:
        uploaded = api.upload(repository, release["id"], file)
        if uploaded.get("digest") != expected or uploaded.get("state") != "uploaded":
            raise ValueError(f"Uploaded asset did not verify: {file.name}")
        print(f"Attached and verified {file.name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["package", "manifest", "publish"])
    parser.add_argument("--version", required=True, type=validate_version)
    parser.add_argument("--directory", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--platform", choices=["windows", "linux"])
    parser.add_argument("--repository")
    parser.add_argument("--tag")
    parser.add_argument("--commit")
    args = parser.parse_args()
    if args.command == "package":
        if not args.platform or not args.output:
            parser.error("package needs --platform and --output")
        package(args.directory, args.output, args.platform, args.version)
    elif args.command == "manifest":
        make_manifest(args.directory, args.version)
    else:
        if not args.repository or not args.tag or not args.commit:
            parser.error("publish needs --repository, --tag and --commit")
        publish(args.directory, args.version, args.repository, args.tag, args.commit,
                GitHub(os.environ["GH_TOKEN"]))


if __name__ == "__main__":
    main()
