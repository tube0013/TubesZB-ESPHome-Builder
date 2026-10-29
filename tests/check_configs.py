"""Check every entry point, reporting the full diagnostics only on failure."""

from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
paths = sorted(root.glob("manifests/*.yaml")) + sorted(root.glob("esphome-config/*.yaml"))
failures = []
for path in paths:
    result = subprocess.run([sys.executable, "-m", "esphome", "config", str(path)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    relative = path.relative_to(root)
    if result.returncode:
        failures.append(relative)
        print(result.stdout)
    print(f"{'FAIL' if result.returncode else 'PASS'} {relative}", flush=True)
print(f"{len(paths) - len(failures)}/{len(paths)} ESPHome configurations validated.")
sys.exit(bool(failures))
