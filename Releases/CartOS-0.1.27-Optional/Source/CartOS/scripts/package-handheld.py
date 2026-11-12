#!/usr/bin/env python3
"""Create the ready-to-copy OnionOS ZIP from a completed device build."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import struct
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CHANGELOG = next((path for path in (
    ROOT / "CHANGELOG.md",
    ROOT.parent / "CHANGELOG.md",
    ROOT.parent.parent / "CHANGELOG.md",
) if path.is_file()), ROOT.parent / "CHANGELOG.md")


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise SystemExit(f"Cannot package CartOS: missing {label}: {path}")


def require_arm_elf(path: Path, label: str) -> None:
    require_file(path, label)
    with path.open("rb") as stream:
        header = stream.read(20)
    if len(header) < 20 or header[:4] != b"\x7fELF" or header[4] != 1 or header[5] != 1:
        raise SystemExit(f"Cannot package CartOS: {label} is not a 32-bit little-endian ELF file")
    machine = struct.unpack_from("<H", header, 18)[0]
    if machine != 40:
        raise SystemExit(f"Cannot package CartOS: {label} is not an ARM executable")


def add_tree(archive: zipfile.ZipFile, source: Path, archive_root: str) -> None:
    for path in sorted(source.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(source)
        if any(part in {".git", "build", "dist", "__pycache__"} for part in relative.parts):
            continue
        if relative.parts[:4] == ("packaging", "OnionOS", "App", "CartOS"):
            continue
        if path.name == ".DS_Store" or path.name.startswith("._") or path.suffix == ".pyc":
            continue
        arcname = f"{archive_root}/{relative.as_posix()}"
        # Store symlink targets as ordinary files so extraction works in Finder,
        # Windows Explorer, and the device's FAT32/exFAT card filesystem.
        archive.write(path.resolve() if path.is_symlink() else path, arcname)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-dir", type=Path,
                        default=ROOT / "packaging/OnionOS/App/CartOS",
                        help="completed OnionOS App/CartOS build folder")
    parser.add_argument("--sdl-revision", required=True,
                        help="Git revision used to build the bundled Miyoo SDL2 runtime")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist",
                        help="folder in which to write the ZIP")
    parser.add_argument("--sdl-source-dir", type=Path, default=ROOT.parent / "SDL2-Miyoo",
                        help="matching SDL source archive and build notes")
    parser.add_argument("--beta", action="store_true", default=False,
                        help="label this package as a Beta build")
    parser.add_argument("--optional", action="store_true",
                        help="name this release separately from the standard download")
    args = parser.parse_args()

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    version_match = re.search(r"project\(CartOS VERSION ([0-9.]+)", cmake)
    if not version_match:
        raise SystemExit("Cannot determine CartOS version from CMakeLists.txt")
    version = version_match.group(1)

    require_file(args.sdl_source_dir / "SDL2-Miyoo-source.tar.gz", "matching SDL2 source archive")
    require_file(args.sdl_source_dir / "BUILD-NOTES.txt", "SDL2 build notes")
    app = args.app_dir.resolve()
    require_arm_elf(app / "bin/cartos", "CartOS launcher")
    require_arm_elf(app / "lib/libneonarmmiyoo.so", "CartOS SDL compatibility library")
    require_arm_elf(app / "lib/libSDL2-2.0.so.0", "Miyoo SDL2 runtime")
    for relative in (
        "config.json", "full_resolution", "launch.sh", "res/font_atlas.png",
        "res/icon/cartos.png", "res/icon/accessibility.png", "res/onionos_logo.png", "licenses/DejaVu-LICENSE.txt",
        "licenses/SDL2/LICENSE", "licenses/SDL2/LICENSE.txt",
    ):
        require_file(app / relative, relative)
    if any(app.rglob("NEON-LICENSE-NOT-PROVIDED.txt")):
        raise SystemExit("Cannot package CartOS while the unlicensed helper notice is present")
    require_file(ROOT / "INSTALL.txt", "installation guide")
    startup_hook = ROOT / "packaging/OnionOS/startup/CartOS.sh"
    require_file(startup_hook, "optional OnionOS boot and GameSwitcher hook")
    restore_hook = ROOT / "packaging/OnionOS/startup/Restore-OnionOS.sh"
    require_file(restore_hook, "optional OnionOS runtime restore script")
    require_file(ROOT / "LICENSE", "CartOS source license")
    require_file(ROOT / "THIRD-PARTY-NOTICES.md", "third-party notices")
    require_file(CHANGELOG, "patch notes")
    version_label = f"{version} Beta" if args.beta else version
    package_name = f"CartOS-{version}" + ("-Beta" if args.beta else "-Optional" if args.optional else "")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    destination = args.output_dir / f"{package_name}.zip"
    with tempfile.NamedTemporaryFile(prefix=f".{package_name}-", suffix=".zip",
                                     dir=args.output_dir, delete=False) as temporary:
        temporary_path = Path(temporary.name)

    try:
        with zipfile.ZipFile(temporary_path, "w", compression=zipfile.ZIP_DEFLATED,
                             compresslevel=8) as archive:
            package_root = package_name
            add_tree(archive, app, f"{package_root}/App/CartOS")
            for filename in ("INSTALL.txt", "README.md", "ACCESSIBILITY.md", "LICENSE", "THIRD-PARTY-NOTICES.md", "CHANGELOG.md"):
                archive.write(CHANGELOG if filename == "CHANGELOG.md" else ROOT / filename,
                              f"{package_root}/{filename}")
            archive.write(startup_hook, f"{package_root}/Startup/CartOS.sh")
            archive.write(restore_hook, f"{package_root}/Return/Restore-OnionOS.sh")
            build_info = (
                f"CartOS version: {version_label}\n"
                f"Miyoo SDL2 backend source: https://github.com/XK9274/sdl2_miyoo\n"
                f"Miyoo SDL2 backend revision: {args.sdl_revision}\n"
                "SDL2 build option: OSS audio enabled for OnionOS libpadsp.\n"
                "SDL2 audio patch: virtual /dev/dsp enumeration (source included).\n"
                "Matching backend source and build notes: Source/SDL2-Miyoo/\n"
            )
            archive.writestr(f"{package_root}/BUILD-INFO.txt", build_info)
            archive.writestr(
                f"{package_root}/Source/SDL2-Miyoo-SOURCE.txt",
                "Matching source and build notes for the bundled Miyoo SDL2 runtime are included in Source/SDL2-Miyoo/.\n"
                "Source revision: " + args.sdl_revision + "\n"
                "OSS audio is enabled for OnionOS libpadsp; see BUILD-NOTES.txt.\n",
            )
            add_tree(archive, args.sdl_source_dir, f"{package_root}/Source/SDL2-Miyoo")
            add_tree(archive, ROOT, f"{package_root}/Source/CartOS")
        os.chmod(temporary_path, 0o644)
        os.replace(temporary_path, destination)
    except Exception:
        temporary_path.unlink(missing_ok=True)
        raise

    print(f"Created install package: {destination}")


if __name__ == "__main__":
    main()
