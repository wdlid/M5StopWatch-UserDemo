#!/usr/bin/env python3
"""
Automatically parse and increment the firmware version in main/apps/common/common.h.
"""
import sys
import re
import os
import subprocess

def find_repo_root():
    try:
        out = subprocess.check_output(["git", "rev-parse", "--show-toplevel"], text=True)
        return out.strip()
    except Exception:
        # Fallback to parent directory of scripts
        return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def parse_and_bump(version_str):
    # Matches strings like V0.5, v0.5, 0.5, V0.5.1, v1.0.0
    match = re.match(r"^([vV]?)([0-9]+(?:\.[0-9]+)*)$", version_str.strip())
    if not match:
        raise ValueError(f"Unsupported version format: '{version_str}'")

    prefix = match.group(1)
    parts_str = match.group(2).split(".")
    parts = [int(p) for p in parts_str]

    # Bump the last numeric segment
    parts[-1] += 1

    new_num_str = ".".join(str(p) for p in parts)
    new_version = f"{prefix}{new_num_str}"
    # Tag is canonical lowercase 'v' prefix
    new_tag = f"v{new_num_str}"
    return new_version, new_tag

def main():
    repo_root = find_repo_root()
    common_h_path = os.path.join(repo_root, "main", "apps", "common", "common.h")
    audio_common_h_path = os.path.join(repo_root, "main", "apps", "common", "audio", "common.h")

    if not os.path.isfile(common_h_path):
        print(f"Error: {common_h_path} not found!", file=sys.stderr)
        sys.exit(1)

    with open(common_h_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Search for FirmwareVersion = "...";
    pattern = re.compile(r'(inline\s+constexpr\s+std::string_view\s+FirmwareVersion\s*=\s*")([^"]+)(";)')
    match = pattern.search(content)
    if not match:
        print(f"Error: Could not find FirmwareVersion declaration in {common_h_path}", file=sys.stderr)
        sys.exit(1)

    old_version = match.group(2)

    if "--current" in sys.argv:
        tag = f"v{old_version.lstrip('vV')}"
        print(f"CURRENT_VERSION={old_version}")
        print(f"CURRENT_TAG={tag}")
        return

    new_version, new_tag = parse_and_bump(old_version)

    if "--dry-run" in sys.argv:
        print(f"OLD_VERSION={old_version}")
        print(f"NEW_VERSION={new_version}")
        print(f"NEW_TAG={new_tag}")
        return

    new_content = pattern.sub(rf'\g<1>{new_version}\g<3>', content)
    with open(common_h_path, "w", encoding="utf-8") as f:
        f.write(new_content)

    if os.path.isfile(audio_common_h_path):
        with open(audio_common_h_path, "r", encoding="utf-8") as f:
            a_content = f.read()
        a_new_content = pattern.sub(rf'\g<1>{new_version}\g<3>', a_content)
        with open(audio_common_h_path, "w", encoding="utf-8") as f:
            f.write(a_new_content)

    print(f"OLD_VERSION={old_version}")
    print(f"NEW_VERSION={new_version}")
    print(f"NEW_TAG={new_tag}")

if __name__ == "__main__":
    main()
