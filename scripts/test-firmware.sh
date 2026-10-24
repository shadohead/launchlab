#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
TEST_BUILD=$(mktemp -d)
trap 'rm -rf "$TEST_BUILD"' EXIT
# Compile objects separately so ccache can validate each translation unit.
# Link and execute every sanitizer test on every run, even on a cache hit.
CXX_COMMAND=("${CXX:-c++}")
if [ "${LAUNCHLAB_CCACHE:-0}" = 1 ]; then
  command -v ccache >/dev/null
  CXX_COMMAND=(ccache "${CXX:-c++}")
fi
FLAGS=(-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
INCLUDES=(-Ifirmware/tests/fakes -Ifirmware/LaunchLabRpm -Ifirmware/LaunchLabMini)
# All suites use this exact VQF configuration; it is an independent translation
# unit, so compile it once rather than once for each test executable.
"${CXX_COMMAND[@]}" "${FLAGS[@]}" -DVQF_SINGLE_PRECISION "${INCLUDES[@]}" \
  -c firmware/LaunchLabMini/src/vqf/vqf.cpp -o "$TEST_BUILD/vqf.o"
for suite in accel_range bmi270_fifo i2c_bus_recovery appearance nvs_budget best_effect_sound device_backup device_wipe wipe_confirmation sticks3 sticks3_level sticks3_power motion_activity sensor_idle launch_feedback launch_motion motion_replay motion_store practice_history practice_store orientation_estimator mount_orientation mount_orientation_setting analog_tachometer swing_reset edge_start edge_interpolation start_mark_reference startup_single_peak paired_rpm_estimators rpm_hardening rpm_estimator_setting sensor_profile_setting display_flip wake_ui_state controls controls_ui power_diagnostics weak_slowdown power_signal_recovery store_presence practice_views history_idle_checkpoint usb_awake usb_tx hil_resync usb_command_guard pm1_idle power_key palette milestones physical_effects classic_effects elemental_effects eva_effects slayer_effects ninja_effects nier_effects sorcery_effects aura_effects alchemy_effects phantom_effects armory_effects grimoire_effects ripsaw_effects corsair_effects reaper_effects flash_log effect_set effect_store fx_transfer loaded_effects fx_effects fx_sound fx_music music_stream fx_fuzz; do
  "${CXX_COMMAND[@]}" "${FLAGS[@]}" -DVQF_SINGLE_PRECISION "${INCLUDES[@]}" \
    -c "firmware/tests/test_${suite}.cpp" -o "$TEST_BUILD/$suite.o"
  "${CXX:-c++}" "${FLAGS[@]}" "$TEST_BUILD/$suite.o" "$TEST_BUILD/vqf.o" -o "$TEST_BUILD/$suite"
  "$TEST_BUILD/$suite"
done
# Share codes inflate with zlib.
"${CXX_COMMAND[@]}" "${FLAGS[@]}" "${INCLUDES[@]}" -c firmware/tests/test_fx_share.cpp -o "$TEST_BUILD/fx_share.o"
"${CXX:-c++}" "${FLAGS[@]}" "$TEST_BUILD/fx_share.o" -lz -o "$TEST_BUILD/fx_share"
"$TEST_BUILD/fx_share"
# The production optical owner on the synchronous ESP-IDF stand-ins.
"${CXX_COMMAND[@]}" "${FLAGS[@]}" -Ifirmware/tests/fakes/acquisition_host "${INCLUDES[@]}" \
  -c firmware/tests/test_acquisition_owner.cpp -o "$TEST_BUILD/acquisition_owner.o"
"${CXX:-c++}" "${FLAGS[@]}" "$TEST_BUILD/acquisition_owner.o" -o "$TEST_BUILD/acquisition_owner"
"$TEST_BUILD/acquisition_owner"
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
python3 firmware/tests/check_save_paths.py
python3 firmware/tests/check_cpu_power_pipeline.py
python3 firmware/tests/check_usb_flush.py
python3 firmware/tests/check_usb_command_pipeline.py
python3 firmware/tests/check_main_layout.py
python3 firmware/tests/check_usb_config.py
