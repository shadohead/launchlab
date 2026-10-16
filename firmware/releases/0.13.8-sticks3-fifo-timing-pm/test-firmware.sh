#!/usr/bin/env bash
set -euo pipefail
ARCHIVE_ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$ARCHIVE_ROOT/../../.." && pwd)"
if [[ ! -f "$REPO_ROOT/firmware/tests/test_accel_range.cpp" || ! -f "$ARCHIVE_ROOT/source/LaunchLabMini.ino" ]]; then
  echo 'Run these archived tests inside their matching full LaunchLab checkout.' >&2
  exit 1
fi
# Reuse this checkout's harnesses, but test the frozen production source.
TEST_ROOT=$(mktemp -d)
trap 'rm -rf "$TEST_ROOT"' EXIT
mkdir -p "$TEST_ROOT/firmware" "$TEST_ROOT/build"
cp -R "$REPO_ROOT/firmware/tests" "$REPO_ROOT/firmware/LaunchLabRpm" "$TEST_ROOT/firmware/"
# Native estimator comparisons include preserved historical headers.
ln -s "$REPO_ROOT/firmware/releases" "$TEST_ROOT/firmware/releases"
cp -R "$ARCHIVE_ROOT/source" "$TEST_ROOT/firmware/LaunchLabMini"
TEST_BUILD="$TEST_ROOT/build"
cd "$TEST_ROOT"
for suite in accel_range bmi270_fifo appearance nvs_budget best_effect_sound device_backup device_wipe wipe_confirmation sticks3 sticks3_level sticks3_power motion_activity sensor_idle launch_feedback launch_motion motion_replay motion_store practice_history practice_store orientation_estimator mount_orientation mount_orientation_setting analog_tachometer swing_reset edge_start startup_single_peak paired_rpm_estimators rpm_estimator_setting sensor_profile_setting display_flip wake_ui_state controls controls_ui power_diagnostics weak_slowdown power_signal_recovery store_presence practice_views history_idle_checkpoint usb_awake pm1_idle power_key; do
  "${CXX:-c++}" -std=c++17 -O1 -g -DVQF_SINGLE_PRECISION -fsanitize=address,undefined -fno-omit-frame-pointer -Ifirmware/tests/fakes -Ifirmware/LaunchLabRpm -Ifirmware/LaunchLabMini "firmware/tests/test_${suite}.cpp" firmware/LaunchLabMini/src/vqf/vqf.cpp -o "$TEST_BUILD/$suite"
  "$TEST_BUILD/$suite"
done
"${CXX:-c++}" -std=c++17 -O1 -g -DVQF_SINGLE_PRECISION -fsanitize=address,undefined -fno-omit-frame-pointer -Ifirmware/tests/fakes/motion -Ifirmware/LaunchLabMini firmware/tests/test_motion_acquisition_timing.cpp firmware/LaunchLabMini/src/vqf/vqf.cpp -o "$TEST_BUILD/motion_acquisition_timing"
"$TEST_BUILD/motion_acquisition_timing"
python3 firmware/tests/check_checkpoint_barrier.py
python3 firmware/tests/check_motion_playback.py
python3 firmware/tests/check_motion_pipeline.py
python3 firmware/tests/check_power_log_retry.py
python3 firmware/tests/check_history_retry.py
python3 firmware/tests/check_usb_pipeline.py
python3 firmware/tests/check_imu_startup.py
python3 firmware/tests/check_mount_pipeline.py
python3 firmware/tests/check_wipe_pipeline.py
python3 firmware/tests/check_controls_pipeline.py
python3 firmware/tests/check_best_sound_pipeline.py
python3 firmware/tests/check_pm1_idle.py
python3 firmware/tests/check_power_key_pipeline.py
python3 firmware/tests/check_idle_power_pipeline.py
python3 firmware/tests/check_sensor_idle_pipeline.py
python3 firmware/tests/check_cpu_power_pipeline.py
python3 firmware/tests/check_main_layout.py
