"""Release publication failures must not overwrite or partially replace assets."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "release_assets", Path(__file__).resolve().parents[1] / "tools/release_assets.py")
release_assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release_assets)
COMMIT = "a" * 40


class FakeGitHub:
    def __init__(self, assets=(), commit=COMMIT, bad_upload=False):
        self.assets, self.commit = list(assets), commit
        self.uploads, self.bad_upload = [], bad_upload

    def request(self, path):
        if "/commits/" in path:
            return {"sha": self.commit}
        return {"id": 1, "tag_name": "v1.2.0", "draft": False, "assets": self.assets}

    def upload(self, repository, release_id, file):
        self.uploads.append(file.name)
        return {"state": "uploaded", "digest": "sha256:" + (
            "bad" if self.bad_upload else release_assets.digest(file))}


class ReleaseGuards(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        for name in release_assets.archive_names("1.2.0"):
            (self.directory / name).write_bytes(b"tested runtime archive")

    def tearDown(self):
        self.temporary.cleanup()

    def publish(self, api, tag="v1.2.0"):
        release_assets.publish(self.directory, "1.2.0", "owner/repo", tag, COMMIT, api)

    def test_one_missing_platform_blocks_all_uploads(self):
        (self.directory / release_assets.archive_names("1.2.0")[1]).unlink()
        api = FakeGitHub()
        with self.assertRaisesRegex(ValueError, "exactly one Windows and one Linux"):
            self.publish(api)
        self.assertEqual(api.uploads, [])

    def test_unexpected_file_blocks_bundle(self):
        (self.directory / "BTECH.EXE").write_bytes(b"must not be released")
        with self.assertRaises(ValueError):
            release_assets.make_manifest(self.directory, "1.2.0")

    def test_wrong_tag_blocks_uploads(self):
        api = FakeGitHub()
        with self.assertRaisesRegex(ValueError, "tag must match"):
            self.publish(api, "v1.0.0")
        self.assertEqual(api.uploads, [])

    def test_moved_tag_blocks_uploads(self):
        api = FakeGitHub(commit="b" * 40)
        with self.assertRaisesRegex(ValueError, "tag moved"):
            self.publish(api)
        self.assertEqual(api.uploads, [])

    def test_conflict_on_second_archive_preflights_before_any_upload(self):
        api = FakeGitHub(assets=[{
            "name": release_assets.archive_names("1.2.0")[1],
            "digest": "sha256:different", "state": "uploaded"}])
        with self.assertRaisesRegex(ValueError, "Existing release asset differs"):
            self.publish(api)
        self.assertEqual(api.uploads, [])

    def test_retry_skips_identical_assets(self):
        files = release_assets.make_manifest(self.directory, "1.2.0")
        api = FakeGitHub(assets=[{"name": file.name, "state": "uploaded",
                                  "digest": "sha256:" + release_assets.digest(file)}
                                 for file in files])
        self.publish(api)
        self.assertEqual(api.uploads, [])

    def test_success_uploads_exactly_the_two_archives_and_manifest(self):
        api = FakeGitHub()
        self.publish(api)
        self.assertEqual(api.uploads, release_assets.archive_names("1.2.0")
                         + ["CrescentHawksInception-1.2.0-SHA256SUMS.txt"])

    def test_upload_digest_is_verified(self):
        api = FakeGitHub(bad_upload=True)
        with self.assertRaisesRegex(ValueError, "Uploaded asset did not verify"):
            self.publish(api)


if __name__ == "__main__":
    unittest.main()
