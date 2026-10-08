#!/usr/bin/env python3
"""
sync_build_info.py
Automatically synchronizes src/core/BuildInfo.h and src/api/WebUI.h,
and stages them into the current git commit during the pre-commit hook.
Eliminates any need for separate 'chore(build): sync BuildInfo' commits.
"""
import os
import sys
import subprocess
import runpy

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

def main():
    # 1. Run build_webui.py to ensure WebUI.h and BuildInfo.h are up to date
    build_script = os.path.join(ROOT_DIR, "scripts", "build_webui.py")
    if os.path.exists(build_script):
        runpy.run_path(build_script)

    # 2. If running inside a git repository, auto-stage the generated artifacts
    git_dir = os.path.join(ROOT_DIR, ".git")
    if os.path.exists(git_dir):
        files_to_stage = [
            os.path.join("src", "core", "BuildInfo.h"),
            os.path.join("src", "api", "WebUI.h")
        ]
        existing = [f for f in files_to_stage if os.path.exists(os.path.join(ROOT_DIR, f))]
        if existing:
            try:
                subprocess.run(["git", "add"] + existing, cwd=ROOT_DIR, check=True)
                print(f"✅ Auto-staged build artifacts: {', '.join(existing)}")
            except Exception as e:
                print(f"⚠️ Warning: Could not auto-stage build artifacts: {e}")

if __name__ == "__main__":
    main()
