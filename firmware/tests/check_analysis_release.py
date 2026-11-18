"""Check the RC identity and keep offline training dependencies out of the sketch."""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "scripts"))
from local_firmware import release_identity

identity = release_identity()
assert identity["analysis_version"] == "1.0-rc.5"
assert identity["runtime"] == "0.20.11-analysis-1.0-rc.5-pm"
assert len(identity["runtime"].encode()) < 32  # USB backup runtime field.

sketch = root / "firmware/LaunchLabMini"
pending = [sketch / "LaunchLabMini.ino"]
visited = set()
while pending:
    path = pending.pop().resolve()
    if path in visited:
        continue
    visited.add(path)
    source = path.read_text(encoding="utf-8")
    for include in re.findall(r'^\s*#include\s+"([^"]+)"', source, re.MULTILINE):
        dependency = (path.parent / include).resolve()
        if not dependency.is_file():
            continue  # External library headers are checked by the firmware build.
        assert dependency.is_relative_to(root / "firmware/LaunchLabMini") or dependency.is_relative_to(
            root / "firmware/LaunchLabRpm"
        ), f"Offline dependency reached the default sketch: {dependency}"
        pending.append(dependency)

names = {path.name for path in visited}
assert {"fixed_riccati.h", "stamina_finish.h", "stamina_pace.h", "stamina_weights.h", "firmware_version.h"} <= names
assert {"audio_finish.h", "audio_finish_weights.h", "audio_finish_features.h"} <= names
assert not any("candidate" in name or "offline" in name for name in names)
print("PASS: Analysis RC identity, current predictor, and offline model isolation")
