#pragma once

// Analysis has its own release cycle; retain the base firmware version.
#define LAUNCHLAB_ANALYSIS_VERSION "1.0-rc.5"
#define LAUNCHLAB_RUNTIME_PM "0.20.11-analysis-1.0-rc.5-pm"

#if LAUNCHLAB_HIL
static constexpr char VERSION[] = "0.20.11-analysis-1.0-rc.5-hil";
#elif LAUNCHLAB_CPU_PM
static constexpr char VERSION[] = LAUNCHLAB_RUNTIME_PM;
#else
static constexpr char VERSION[] = "0.20.11-analysis-1.0-rc.5";
#endif
