#!/usr/bin/env python3

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path


IDF_VERSION = "5.5.5"
IDF_IMAGE = "espressif/idf@sha256:a9231d0697ab8f7517cc072e93b7c83e04907bfbfba80b6440d7dbbf90665cf2"
TARGETS = {
    "esp32": "esp32",
    "esp32s3": "esp32s3",
    "esp32c3": "esp32c3",
}
ROLES = ("peripheral", "central")


def repo_root():
    for parent in Path(__file__).resolve().parents:
        if (parent / "AGENTS.md").is_file():
            return parent
    raise RuntimeError("repository root not found")


def run(command):
    subprocess.run(command, check=True)


def build_target(root, source, work_root, target):
    outputs = []
    for role in ROLES:
        role_dir = work_root / target / role
        build_dir = role_dir / "build"
        sdkconfig = role_dir / "sdkconfig"
        build_dir.mkdir(parents=True)
        container_build = f"/project/{build_dir.relative_to(root)}"
        container_sdkconfig = f"/project/{sdkconfig.relative_to(root)}"
        run([
            "docker", "run", "--rm", "--platform", "linux/arm64",
            "-e", f"IDF_TARGET={target}",
            "-v", f"{root}:/project",
            "-w", f"/project/{(source / role).relative_to(root)}",
            IDF_IMAGE,
            "idf.py", "-B", container_build,
            "-D", f"SDKCONFIG={container_sdkconfig}", "build",
        ])

        flasher_args = json.loads(
            (build_dir / "flasher_args.json").read_text(encoding="utf-8")
        )
        settings = flasher_args["flash_settings"]
        flash_files = sorted(
            flasher_args["flash_files"].items(),
            key=lambda item: int(item[0], 0),
        )
        merged = role_dir / f"ble_gatt_{role}.merged.bin"
        command = [
            "docker", "run", "--rm", "--platform", "linux/arm64",
            "-v", f"{root}:/project",
            "-w", container_build,
            IDF_IMAGE,
            "esptool.py", "--chip", target, "merge_bin",
            "-o", f"/project/{merged.relative_to(root)}",
            "--flash_mode", settings["flash_mode"],
            "--flash_freq", settings["flash_freq"],
            "--flash_size", "2MB" if settings["flash_size"] == "detect" else settings["flash_size"],
            "--fill-flash-size", "2MB",
        ]
        for offset, filename in flash_files:
            command.extend((offset, filename))
        run(command)

        destination = root / "resources/data/bin" / target / merged.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(merged, destination)
        digest = hashlib.sha256(destination.read_bytes()).hexdigest()
        outputs.append((destination.relative_to(root), digest))
    return outputs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("targets", nargs="*", choices=TARGETS, default=list(TARGETS))
    args = parser.parse_args()
    root = repo_root()
    source = Path(__file__).resolve().parent
    work_root = root / "tmp/ble-gatt-release" / IDF_VERSION
    shutil.rmtree(work_root.parent, ignore_errors=True)
    try:
        outputs = []
        for target in args.targets:
            outputs.extend(build_target(root, source, work_root, TARGETS[target]))
        for path, digest in outputs:
            print(f"{digest}  {path}")
    finally:
        shutil.rmtree(work_root.parent, ignore_errors=True)


if __name__ == "__main__":
    main()
