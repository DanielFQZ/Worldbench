"""Reject stale, untested or mismatched distributable builds before packaging."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

REQUIRED_TESTS = {
    "worldbench_core", "worldbench_storage", "worldbench_creation", "worldbench_ui",
    "worldbench_creation_ui", "worldbench_render", "worldbench_updates", "worldbench_update_ui",
    "worldbench_startup", "worldbench_zip_install", "worldbench_silent_install",
    "worldbench_export_decoder", "worldbench_project_session", "worldbench_stability_core",
    "worldbench_stability_ui", "worldbench_release_gate", "worldbench_executable_mcp",
    "worldbench_surface", "worldbench_region", "worldbench_preferences", "worldbench_assets",
    "worldbench_terrain", "worldbench_theme", "worldbench_smart_blend", "worldbench_far_scene",
    "worldbench_display_options", "worldbench_water", "worldbench_surface_preview",
}

def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

def validate(dist, version, source_commit, source_clean):
    dist = Path(dist)
    info = json.loads((dist / "build-info.json").read_text(encoding="utf-8-sig"))
    if not source_clean or info.get("source_clean") is not True:
        raise ValueError("Release requires a clean committed source tree")
    if info.get("version") != version or info.get("source_commit") != source_commit:
        raise ValueError("Build version/source commit differs from release source")
    for name, key in (("Worldbench.exe", "executable_sha256"), ("WorldbenchMcp.exe", "mcp_sha256")):
        if digest(dist / name) != info.get(key):
            raise ValueError(f"Binary digest mismatch: {name}")
    report = dist / "release-tests.xml"
    if digest(report) != info.get("test_report_sha256") or info.get("tests_passed") is not True:
        raise ValueError("Missing or changed successful test report")
    root = ET.parse(report).getroot()
    cases = list(root.iter("testcase"))
    names = [case.get("name") for case in cases]
    if not REQUIRED_TESTS.issubset(names) or len(set(names)) != len(names):
        raise ValueError("Release test coverage is incomplete or duplicated")
    if len(cases) != info.get("test_count") or any(
        case.get("status") in ("notrun", "fail", "disabled") or
        any(case.find(tag) is not None for tag in ("failure", "error", "skipped")) for case in cases
    ):
        raise ValueError("A release test failed, was skipped or was not run")
    return info

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("version")
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=args.root, text=True).strip()
    clean = not subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=all"], cwd=args.root, text=True).strip()
    try:
        info = validate(args.root / "dist" / f"Worldbench-{args.version}", args.version, commit, clean)
    except (OSError, ValueError, ET.ParseError) as error:
        raise SystemExit(f"Release validation failed: {error}")
    print(f"Release build verified: {args.version}, {commit}, {info['test_count']} tests")
