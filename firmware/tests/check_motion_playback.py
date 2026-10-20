#!/usr/bin/env python3
"""Compile the actual recap call/scheduling predicate into a native timing test.

Extracting these tiny .ino blocks catches wiring regressions as well as helper
regressions without pretending a host fake verifies device rendering latency.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile


def balanced(text, opening):
    depth = 0
    for i in range(opening, len(text)):
        depth += text[i] == "("
        depth -= text[i] == ")"
        if depth == 0:
            return i
    raise RuntimeError("Unbalanced firmware expression")


def split_args(text):
    depth, start, args = 0, 0, []
    for i, char in enumerate(text):
        depth += char == "("
        depth -= char == ")"
        if char == "," and depth == 0:
            args.append(text[start:i])
            start = i + 1
    return args + [text[start:]]


def function(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for cursor in range(opening, len(text)):
        depth += (text[cursor] == "{") - (text[cursor] == "}")
        if depth == 0:
            return text[start:cursor + 1]
    raise RuntimeError("Unbalanced firmware function")


def main():
    root = Path(__file__).resolve().parents[2]
    source = (root / "firmware/LaunchLabMini/LaunchLabMini.ino").read_text()
    call_start = source.index("MotionUI::feedback(")
    call_open = source.index("(", call_start)
    call_end = balanced(source, call_open)
    call = source[call_start:call_end + 1]
    args = split_args(source[call_open + 1:call_end])
    progress_index = next(i for i, arg in enumerate(args) if "MotionReplay::playbackProgress(" in arg)
    args[progress_index] = " expectedProgress"
    expected_call = "MotionUI::feedback(" + ",".join(args) + ")"
    hook_start = source.index("launchFeedback.replayPresented(")
    hook_open = source.index("(", hook_start)
    hook = source[hook_start:balanced(source, hook_open) + 1]
    match = re.search(r"if\(!tournamentMode[^;]+launchFeedback\.shown\(\)[^;]+\)dirty=true;", source)
    if not match:
        raise RuntimeError("Automatic recap redraw predicate not found")
    conditional = match[0]
    opening = conditional.index("(")
    condition = conditional[opening + 1:balanced(conditional, opening)]
    frame_time = re.search(r"const uint32_t replayFrameElapsed=[^;]+;", source)[0]
    header = (
        function(source, "static bool visibleDisplayFlipped()") + "\n"
        + function(source, "static const char *celebrationBadge()") + "\n"
        + function(source, "static void drawLiveLevel(") + "\n"
        + "static uint32_t drawActualFeedback(lgfx::LGFXBase &view){const Palette::Colors colors=ui();" + frame_time + call + ";return replayFrameElapsed;}\n"
        "static void drawExpectedFeedback(lgfx::LGFXBase &view,float expectedProgress){const Palette::Colors colors=ui();" + expected_call + ";}\n"
        "static void finishActualFeedbackFrame(uint32_t replayFrameElapsed){" + hook + ";}\n"
        "static bool actualNeedsReplayFrame(uint32_t now,uint32_t lastDraw){return " + condition + ";}\n"
    )
    with tempfile.TemporaryDirectory(prefix="launchlab-tilt-") as directory:
        build = Path(directory)
        (build / "motion_playback_wiring.h").write_text(header)
        executable = build / "motion-playback"
        compiler = os.environ.get("CXX", "c++")
        subprocess.run([compiler, "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer", "-I" + str(build),
                        str(root / "firmware/tests/test_motion_playback.cpp"), "-o", str(executable)], check=True)
        result = subprocess.run([str(executable)], cwd=root)
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
