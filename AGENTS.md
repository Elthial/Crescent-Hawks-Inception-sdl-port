# Linux / Nix

Build with `nix build`, play with `nix run .`, and develop with `nix develop`.
Run asset-independent checks with `nix flake check` and original-data tests
with `nix run .#integration-tests`. See `flake.nix` for target implementation
and [README.md](README.md#linux-with-nix) for setup.

Never run tests against the original installation; use the integration target's
temporary copy. Do not point `CHI_ASSET_DIRECTORY` at the originals.
