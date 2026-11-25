import os
import shutil
from pathlib import Path

Import("env")

ccache_bin = shutil.which("ccache")
if ccache_bin:
    project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
    shim_dir = project_dir / ".pio" / "ccache-bin"
    shim_dir.mkdir(parents=True, exist_ok=True)

    compilers = (
        "xtensa-esp32s3-elf-gcc",
        "xtensa-esp32s3-elf-g++",
        "xtensa-esp32s3-elf-cc",
        "xtensa-esp32s3-elf-c++",
    )

    for compiler in compilers:
        link_path = shim_dir / compiler
        if link_path.is_symlink():
            if os.readlink(link_path) != ccache_bin:
                link_path.unlink()
                link_path.symlink_to(ccache_bin)
        elif not link_path.exists():
            link_path.symlink_to(ccache_bin)

    shim_str = str(shim_dir)
    env.PrependENVPath("PATH", shim_str)
    current_path = os.environ.get("PATH", "")
    if shim_str not in current_path.split(os.pathsep):
        os.environ["PATH"] = f"{shim_str}{os.pathsep}{current_path}" if current_path else shim_str

    print(f"[StopWatch ccache] Enabled compiler cache via {shim_dir}")
else:
    print("[StopWatch ccache] ccache not found in PATH; skipping")
