{
  description = "The Crescent Hawk's Inception SDL3 preservation port";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in {
      packages = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          runtime = pkgs.stdenv.mkDerivation {
            pname = "crescent-hawks-inception-runtime";
            version = "1.0.0";
            src = self;
            nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
            buildInputs = [ pkgs.sdl3 ];
            # Preserve reconstructed logic despite GCC-specific warnings.
            cmakeFlags = [ "-DCHI_FETCH_SDL=OFF" "-DBUILD_TESTING=ON" "-DCHI_WARNINGS_AS_ERRORS=OFF" ];
            doCheck = true;
            SDL_VIDEODRIVER = "dummy";
            SDL_AUDIODRIVER = "dummy";
            installPhase = ''
              runHook preInstall
              install -Dm755 CrescentHawksInception "$out/bin/CrescentHawksInception"
              install -Dm644 ../config/CrescentHawksInception.ini "$out/bin/CrescentHawksInception.ini"
              runHook postInstall
            '';
          };
          launcher = pkgs.writeShellApplication {
            name = "crescent-hawks-inception";
            runtimeInputs = [ pkgs.coreutils ];
            text = ''
              if [[ "''${1:-}" == --version ]]; then
                exec ${runtime}/bin/CrescentHawksInception --version
              fi
              source_dir="''${CHI_ORIGINAL_ASSET_DIRECTORY:-}"
              data_dir="''${CHI_ASSET_DIRECTORY:-''${XDG_DATA_HOME:-$HOME/.local/share}/crescent-hawks-inception}"
              if [[ -n "$source_dir" && -d "$source_dir" ]]; then
                source_dir=$(realpath -- "$source_dir")
                data_dir=$(realpath -m -- "$data_dir")
                if [[ "$data_dir" == "$source_dir" || "$data_dir" == "$source_dir"/* ]]; then
                  echo "The writable play directory must be outside the original installation." >&2
                  exit 1
                fi
              fi
              if [[ ! -f "$data_dir/BTECH.EXE" ]]; then
                if [[ -z "$source_dir" || ! -f "$source_dir/BTECH.EXE" ]]; then
                  echo "Set CHI_ORIGINAL_ASSET_DIRECTORY to an original game installation containing BTECH.EXE." >&2
                  exit 1
                fi
                mkdir -p "$data_dir"
                cp -RLn -- "$source_dir"/. "$data_dir"/
                chmod -R u+w "$data_dir"
              fi
              export CHI_ASSET_DIRECTORY="$data_dir"
              exec ${runtime}/bin/CrescentHawksInception "$@"
            '';
          };
          integration-tests = pkgs.writeShellApplication {
            name = "chi-integration-tests";
            runtimeInputs = [ pkgs.coreutils pkgs.cmake pkgs.ninja pkgs.stdenv.cc pkgs.pkg-config ];
            text = ''
              source_dir="''${CHI_ORIGINAL_ASSET_DIRECTORY:-}"
              if [[ -z "$source_dir" || ! -f "$source_dir/BTECH.EXE" ]]; then
                echo "Set CHI_ORIGINAL_ASSET_DIRECTORY to an original game installation containing BTECH.EXE." >&2
                exit 1
              fi
              work_dir=$(mktemp -d -t chi-integration-tests.XXXXXXXX)
              cleanup() {
                if [[ "''${CHI_KEEP_TEST_DIRECTORY:-0}" == 1 ]]; then
                  echo "Integration-test files retained at $work_dir" >&2
                else
                  rm -rf -- "$work_dir"
                fi
              }
              trap cleanup EXIT
              trap 'exit 130' INT
              trap 'exit 143' TERM
              mkdir "$work_dir/assets"
              cp -RL -- "$source_dir"/. "$work_dir/assets"/
              chmod -R u+w "$work_dir/assets"
              # Do not inherit a play directory pointing at the originals.
              export CHI_ASSET_DIRECTORY="$work_dir/assets"
              export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
              cmake -S ${self} -B "$work_dir/build" -G Ninja \
                -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
                -DCHI_FETCH_SDL=OFF -DCHI_WARNINGS_AS_ERRORS=OFF \
                -DCMAKE_C_COMPILER=${pkgs.stdenv.cc}/bin/cc \
                -DCMAKE_PREFIX_PATH=${pkgs.sdl3.dev} \
                -DCHI_ORIGINAL_ASSET_DIRECTORY="$work_dir/assets"
              cmake --build "$work_dir/build" --parallel "$(nproc)"
              ctest --test-dir "$work_dir/build" --output-on-failure --parallel 1 "$@"
            '';
          };
        in { default = launcher; inherit runtime integration-tests; });
      checks = forAllSystems (system: { runtime = self.packages.${system}.runtime; });
      devShells = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in { default = pkgs.mkShell {
          packages = [ pkgs.cmake pkgs.ninja pkgs.sdl3 pkgs.pkg-config ];
        }; });
    };
}
