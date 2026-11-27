"""Run the production exporter and the independent NBT verifier together."""
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="Worldbench-export-check-") as temporary:
    output = Path(temporary) / "fixture"
    subprocess.run([sys.argv[1], str(output)], check=True, timeout=120)
    subprocess.run([sys.executable, str(Path(__file__).with_name("verify_export.py")), str(output)],
                   check=True, timeout=120)
