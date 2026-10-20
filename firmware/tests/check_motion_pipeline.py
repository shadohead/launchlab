#!/usr/bin/env python3
"""Execute production prepare/finish/save/draw scheduling with host peripherals."""
import os
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def close(text, opening, left="(", right=")"):
    depth = 0
    for i in range(opening, len(text)):
        depth += text[i] == left
        depth -= text[i] == right
        if depth == 0:
            return i
    raise RuntimeError("Unbalanced production block")


def function(source, name):
    start = source.index("static void " + name + "(")
    opening = source.index("{", start)
    return source[start:close(source, opening, "{", "}") + 1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--trace-log", type=Path, help="Optional local device evidence; never copied into shipping fixtures")
    parser.add_argument("--trace-number", type=int)
    options = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (root / "firmware/LaunchLabMini/LaunchLabMini.ino").read_text()
    ui = (root / "firmware/LaunchLabMini/motion_ui.h").read_text()
    call_start = source.index("MotionUI::feedback(")
    call_open = source.index("(", call_start)
    call = source[call_start:close(source, call_open) + 1]
    arguments = source[call_open + 1:close(source, call_open)]
    depth, last, args = 0, 0, []
    for i, char in enumerate(arguments):
        depth += char == "("
        depth -= char == ")"
        if char == "," and depth == 0:
            args.append(arguments[last:i])
            last = i + 1
    args.append(arguments[last:])
    progress = next(arg for arg in args if "MotionReplay::playbackProgress(" in arg)
    # The actual renderer source-time expression is exercised, not duplicated.
    pose_open = ui.index("(", ui.index("current.at("))
    pose_time = ui[pose_open + 1:close(ui, pose_open)]
    frame_time = re.search(r"const uint32_t replayFrameElapsed=[^;]+;", source)
    frame_time = frame_time[0] if frame_time else "const uint32_t replayFrameElapsed=launchFeedback.elapsed(millis());"
    hook = ""
    if "launchFeedback.replayPresented(" in source:
        start = source.index("launchFeedback.replayPresented(")
        opening = source.index("(", start)
        hook = source[start:close(source, opening) + 1] + ";"
        if "bestEffect.presented(" in source: hook += "if(!motionPending && !motionDemo)bestEffect.presented(millis());"
    start = source.index("  s=acquisition.snapshot();\n  const uint32_t now=millis();")
    stop = source.index("\n  if(Serial && now-lastStatus", start)
    loop = source[start:stop]
    coalesced = "HISTORY_COALESCE_MS" in source
    constants = "\n".join(re.search(r"static constexpr uint32_t " + name + r"=\d+;", source)[0]
                            for name in ["HISTORY_COALESCE_MS", "HISTORY_SAVE_RETRY_MS"]) if coalesced else ""
    # Sketches that schedule by IMU stillness: the launcher is set down.
    still = "static HistoryCheckpoint::Stillness imuStill{true,0};\n" if "imuStill" in source else ""
    header = constants + "\n" + still + "\n".join(function(source, name) for name in ["prepareMotion", "finishMotion", "toggleFeedback", "saveHistory"])
    if "celebrationBadge()" in source:
        start = source.index("static const char *celebrationBadge()")
        header = source[start:close(source, source.index("{", start), "{", "}") + 1] + "\n" + header
    header += "\nstatic int actualSourceTime(const LaunchMotion::Trace &trial,float progress){using namespace MotionReplay;return " + pose_time + ";}\n"
    header += "static float actualProgress(uint32_t replayFrameElapsed){return " + progress + ";}\n"
    header += "static void draw(const Snapshot &s){const Palette::Colors colors=ui();" + frame_time + "lgfx::LGFXBase view;if(launchFeedback.shown() && !tournamentMode && !diagnostics && !historyPage){" + call + ";}recordFrame(view,replayFrameElapsed);advance(drawCostUs);" + hook + "}\n"
    header += "static void productionStep(){Snapshot s;" + loop + "}\n"
    with tempfile.TemporaryDirectory(prefix="launchlab-pipeline-") as directory:
        build = Path(directory)
        trace = "inline LaunchMotion::Trace pipelineTrace(){return syntheticPipelineTrace();}\n"
        if options.trace_log:
            lines = options.trace_log.read_text().splitlines()
            exports = [(i, re.search(r"MOTION_EXPORT role=latest number=(\d+) rpm=([\d.]+) quality=valid count=(\d+) duration_ms=(\d+).*fused=1", line))
                       for i, line in enumerate(lines)]
            exports = [(i, m) for i, m in exports if m and (options.trace_number is None or int(m[1]) == options.trace_number)]
            if not exports:
                raise RuntimeError("Requested valid fused local trace not found")
            index, export = exports[-1]
            points = []
            for line in lines[index + 1:]:
                point = re.search(r"MOTION_POINT role=latest t_ms=(-?\d+).* q=([\d.,-]+)$", line)
                if not point:
                    break
                points.append([int(point[1])] + [round(float(v)*16384) for v in point[2].split(",")])
            if len(points) != int(export[3]):
                raise RuntimeError("Local trace point count mismatch")
            trace = ("inline LaunchMotion::Trace pipelineTrace(){LaunchMotion::Trace t;t.quality=LaunchMotion::Quality::Valid;t.fused=true;t.gravity[2]=1;"
                     + f"t.number={int(export[1])};t.rpm={float(export[2])}f;t.count={len(points)};t.durationMs={int(export[4])};"
                     + "static constexpr int data[][5]={" + ",".join("{" + ",".join(map(str, p)) + "}" for p in points) + "};"
                     + "for(unsigned i=0;i<t.count;++i){t.points[i].ms=data[i][0];for(unsigned j=0;j<4;++j)t.points[i].q[j]=data[i][j+1];}LaunchMotion::recordLevels(t);return t;}\n")
        (build / "motion_pipeline_trace.h").write_text(trace)
        (build / "motion_pipeline_wiring.h").write_text(header)
        executable = build / "motion-pipeline"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O1", "-g",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-DHAS_HISTORY_COALESCING=" + str(int(coalesced)), "-I" + str(build),
                        "-I" + str(root / "firmware/LaunchLabRpm"), "-I"+str(root/"firmware/tests/fakes"), str(root / "firmware/tests/test_motion_pipeline.cpp"), "-o", str(executable)], check=True)
        return subprocess.run([str(executable)], cwd=root).returncode


if __name__ == "__main__":
    raise SystemExit(main())
