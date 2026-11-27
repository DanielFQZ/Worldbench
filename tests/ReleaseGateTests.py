"""Test release rejection paths without running an installer or publishing."""
import sys
sys.dont_write_bytecode = True
import importlib.util
import json
from pathlib import Path
import tempfile
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location("verify_build", Path(__file__).resolve().parents[1] / "scripts/verify-build.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
with tempfile.TemporaryDirectory(prefix="Worldbench-release-gate-") as temporary:
    dist = Path(temporary)
    for name in ("Worldbench.exe", "WorldbenchMcp.exe"):
        (dist / name).write_bytes(name.encode())
    report = ET.Element("testsuite")
    for name in sorted(gate.REQUIRED_TESTS):
        ET.SubElement(report, "testcase", name=name, status="run")
    ET.ElementTree(report).write(dist / "release-tests.xml")
    valid = {"version": "1.0.0-pre.1", "source_commit": "a" * 40, "source_clean": True,
             "executable_sha256": gate.digest(dist / "Worldbench.exe"), "mcp_sha256": gate.digest(dist / "WorldbenchMcp.exe"),
             "tests_passed": True, "test_count": len(gate.REQUIRED_TESTS), "test_report_sha256": gate.digest(dist / "release-tests.xml")}
    def run(info, commit="a" * 40, clean=True):
        (dist / "build-info.json").write_text(json.dumps(info))
        return gate.validate(dist, "1.0.0-pre.1", commit, clean)
    def reject(info, **kwargs):
        try: run(info, **kwargs)
        except ValueError: return
        raise AssertionError("invalid release build accepted")
    assert run(valid)["tests_passed"]
    reject(valid, clean=False)
    reject(valid, commit="b" * 40)
    for key, value in {"version": "old", "source_clean": False, "tests_passed": False,
                       "executable_sha256": "bad", "mcp_sha256": "bad", "test_count": 0,
                       "test_report_sha256": "bad"}.items():
        reject({**valid, key: value})
    ET.SubElement(report[0], "skipped")
    ET.ElementTree(report).write(dist / "release-tests.xml")
    reject({**valid, "test_report_sha256": gate.digest(dist / "release-tests.xml")})
    report.remove(report[0])
    ET.ElementTree(report).write(dist / "release-tests.xml")
    reject({**valid, "test_report_sha256": gate.digest(dist / "release-tests.xml"), "test_count": len(report)})
print("PASS 12 release gate checks")
