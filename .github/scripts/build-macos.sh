#!/bin/bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <dusklight-dir>" >&2
    exit 2
fi

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
sdk_root="$(cd "$1" && pwd)"
if [[ ! -f "$sdk_root/sdk/CMakeLists.txt" ]]; then
    echo "Dusklight directory must contain the mod SDK." >&2
    exit 1
fi

python_bin="${PYTHON_BIN:-python3}"
cmake_bin_dir="$("$python_bin" -c 'import cmake; print(cmake.CMAKE_BIN_DIR)')"
ninja_bin_dir="$("$python_bin" -c 'import ninja; print(ninja.BIN_DIR)')"
cmake="$cmake_bin_dir/cmake"
ninja="$ninja_bin_dir/ninja"
"$cmake" --version
"$ninja" --version

build_root="${BUILD_ROOT:-$repo_root/build/macos-arm64}"
"$cmake" -S "$repo_root" -B "$build_root" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    "-DCMAKE_MAKE_PROGRAM=$ninja" \
    "-DDUSKLIGHT_DIR=$sdk_root"
"$cmake" --build "$build_root" --target better_bloom_package --parallel

package="$build_root/mods/better_bloom.dusk"
"$python_bin" - "$package" "$repo_root" <<'PY'
import json
import pathlib
import struct
import sys
import zipfile

package = pathlib.Path(sys.argv[1])
repo = pathlib.Path(sys.argv[2])
required = (
    "mod.json",
    "lib/macos-arm64/mod.so",
    "res/filter.wgsl",
    "res/composite.wgsl",
    "res/LICENSE.txt",
)

with zipfile.ZipFile(package) as archive:
    for name in required:
        info = archive.getinfo(name)
        if info.file_size == 0:
            raise SystemExit(f"Package entry is empty: {name}")

    manifest = json.loads(archive.read("mod.json"))
    expected = json.loads((repo / "mod.json").read_text())
    if (manifest.get("id"), manifest.get("version")) != (
        expected.get("id"), expected.get("version")
    ):
        raise SystemExit("Packaged manifest does not match source manifest")

    license_text = archive.read("res/LICENSE.txt").decode().replace("\r\n", "\n")
    expected_license = (repo / "LICENSE").read_text().replace("\r\n", "\n")
    if license_text != expected_license:
        raise SystemExit("Packaged license does not match LICENSE")

    binary = archive.read("lib/macos-arm64/mod.so")
    if len(binary) < 8 or binary[:4] != b"\xcf\xfa\xed\xfe":
        raise SystemExit("Packaged library is not a 64-bit little-endian Mach-O binary")
    if struct.unpack_from("<I", binary, 4)[0] != 0x0100000C:
        raise SystemExit("Packaged Mach-O library is not ARM64")
PY

output_root="$repo_root/dist"
mkdir -p "$output_root"
output_package="$output_root/Better Bloom-macos-arm64.dusk"
cp "$package" "$output_package"
package_hash="$(shasum -a 256 "$output_package" | awk '{print $1}')"
printf '%s *%s\n' "$package_hash" "$(basename "$output_package")" \
    > "$output_package.sha256"
echo "Validated macOS ARM64 package: $output_package"
