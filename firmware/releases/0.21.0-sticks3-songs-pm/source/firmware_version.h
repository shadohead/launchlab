#pragma once

// Analysis Mode keeps its own version beside the firmware's. It is
// experimental (0.21.0): the finish forecast is a model still being trained
// and improved, so its screens say so.
#define LAUNCHLAB_ANALYSIS_VERSION "1.0-rc.5"
#define LAUNCHLAB_ANALYSIS_EXPERIMENTAL 1
// The production runtime: the PM VERSION line of LaunchLabMini.ino, which the
// release tools read (check_analysis_release.py keeps the two equal).
#define LAUNCHLAB_RUNTIME_PM "0.21.0-sticks3-songs-pm"
