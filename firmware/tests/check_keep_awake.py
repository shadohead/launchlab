#!/usr/bin/env python3
"""Every way the StickS3 can stay awake on battery must end on its own.

Through 0.20.5 units stayed awake until their battery was flat because of
paths that were each reasonable alone but had no time limit: movement renewed
a used session's sleep timer forever, a refused NVS copy or wake marker
deferred every sleep, steady optical marks counted as a pull forever, and a
held button counted as use for as long as it was held (docs/battery/README.md,
fixes 8-11).

This check lists every place in the sketch that renews the sleep timer
(`inactivity.touch`, `motion`, `begin`) or leaves the inactivity sleep or the
shutdown flush before the unit sleeps, keyed by function and source line, each
with what ends it. A new or changed one fails the check until it is listed
here with its bound and a test that shows the bound (a host test, and for the
power paths a case in the HIL `sleep_blockers` scenario, which `all` and so the
release gate run). Use by a person (a button press, an accepted pull, a USB
command) ends when they stop; anything a sensor, a store, a state machine or
an object pressing on the unit can repeat must have its own limit.
"""
from collections import Counter
from pathlib import Path
import ast
import re
import sys

FIRMWARE = Path(__file__).resolve().parents[1]
SKETCH = FIRMWARE / 'LaunchLabMini/LaunchLabMini.ino'
RUN_HIL = FIRMWARE / 'tools/hil/run_hil.py'

# (function, exact source line, what ends it and the test that shows it)
KEEP_AWAKE = [
    # ---- Renewals of the sleep timer.
    ('pollUsbAwake', '(usbAwake.powerBits()&UsbHostAwake::VIN_MASK) && inactivity.unusedShake())inactivity.touch(now);',
     'a confirmed computer with 5 V: USB power, not battery (check_usb_pipeline)'),
    ('pollUsbAwake', 'if(!inactivity.unusedShake())inactivity.touch(now);',
     'once per USB attach or detach edge (check_usb_pipeline)'),
    ('logWearPull', 'inactivity.touch(millis());',
     'one per Wear check pull, a person pulling (check_wear_pipeline)'),
    ('pollLevel', 'lastActivityMotionUs=sample.activity.lastMotionUs;inactivity.motion(now);',
     'movement: MOTION_LIMIT_MS after the last use, UNUSED_SHAKE_LIMIT_MS after a shake wake (test_motion_activity, HIL sleep_blockers)'),
    ('flushForShutdown', 'inactivity.touch(now);dirty=true;',
     'a pull accepted during the flush, a person pulling (check_history_retry)'),
    ('autoPowerOff', 'usbCancelled=true;inactivity.touch(millis());lastShutdownTry=0;',
     'a computer attached at the last moment: USB power (check_usb_pipeline)'),
    ('pollPowerKey', 'powerKeyRequested=true;powerKeyRequestedAt=now;inactivity.touch(now);',
     'a side key click, which powers off (check_power_key_pipeline)'),
    ('pollRestore', 'inactivity.touch(now);',
     'bytes of a USB restore, which needs a computer and times out (check_usb_pipeline)'),
    ('pollThemeRequest', 'inactivity.touch(now);',
     'bytes of a USB theme upload: computer, timeout (check_hil_themes)'),
    ('pollEffectRequest', 'inactivity.touch(now);',
     'bytes of a USB effect upload: computer, timeout'),
    ('pollMilestoneRequest', 'inactivity.touch(now);',
     'bytes of a USB milestone upload: computer, timeout (check_hil_milestones)'),
    ('setup', 'inactivity.begin(millis(),!keyWake && ((pm1WakeValid && (pm1WakeFlags&0x20)) || wakeCause==ESP_SLEEP_WAKEUP_EXT0));',
     'a boot: a shake wake gets UNUSED_SHAKE_LIMIT_MS (test_motion_activity)'),
    ('setup', 'else inactivity.begin(millis()-InactivityTimer::UNUSED_SHAKE_LIMIT_MS,true);',
     'finishing a USB standby after an unplug: due at once (check_usb_standby_pipeline)'),
    ('runUsbCommand', 'inactivity.touch(millis()-inactivity.timeout());',
     'USB S: makes the sleep due at once'),
    ('pollCommands', "if(c!='p' && c!='P' && c!='C' && c!='\\n' && c!='\\r')inactivity.touch(now);",
     'USB command bytes: a computer, which inhibits sleep anyway (check_usb_command_pipeline)'),
    ('loop', 'inactivity.touch(millis());',
     'buttons via HeldButtonUse: a hold counts LIMIT_MS at most (test_motion_activity)'),
    ('loop', 'inactivity.touch(millis());dirty=true;',
     'a Wear check step, a person pulling (check_wear_pipeline)'),
    ('loop', 'inactivity.touch(millis());',
     'an accepted pull (check_history_retry)'),
    ('loop', 'if(opticalGuard.measuring(now,s.phase==AnalogTachometer::Phase::Launch))inactivity.touch(now);',
     'a pull being measured: OpticalSleepGuard::MEASURE_LIMIT_MS (test_motion_activity, HIL sleep_blockers)'),
    # ---- Ways out of the shutdown flush without sleeping.
    ('flushForShutdown', 'if(!acquisition.request(force?Action::ShutdownForce:Action::ShutdownBegin,s.launches))return ShutdownFlush::Busy;',
     'the detector busy: OpticalSleepGuard forces the stop after FORCE_AFTER refusals (test_acquisition_owner, HIL sleep_blockers)'),
    ('flushForShutdown', 'return ShutdownFlush::NewPull;',
     'a pull accepted during the flush, a person pulling'),
    ('flushForShutdown', 'Serial.printf("POWER_OFF_DEFERRED %s_save_failed\\n",historyFailed?"history":"best");',
     'a save no store holds: with the history log open the log takes it, NVS only a copy (check_save_paths, HIL sleep_blockers); '
     'only a unit whose log did not open stays on, and shows the save error'),
    ('flushForShutdown', 'return ShutdownFlush::SaveFailed;', 'as the line above'),
    ('flushForShutdown', 'return ShutdownFlush::Ready;', 'the sleep goes ahead'),
    # ---- Ways out of the inactivity sleep before it runs.
    ('autoPowerOff', 'if(wipeBlocked || controls.page==Controls::Page::Wipe)return;',
     'the Wipe page closes itself after WipeConfirmation::TIMEOUT_MS; only a failed erase holds it, and saves are blocked there (check_wipe_pipeline)'),
    ('autoPowerOff', 'if(Lab::running)return;  // A lab run measures screen-on time: never sleep.',
     'the battery lab build only, never shipped'),
    ('autoPowerOff', 'if(!Hil::allowSleep.load())return;', 'HIL builds only, never shipped'),
    ('autoPowerOff', 'if(usbAwake.inhibited(now) && !dry)return;',
     'a computer attached: USB power, not battery (check_usb_pipeline)'),
    ('autoPowerOff', 'if(motionPending || !inactivity.due(now,measuring) || (lastShutdownTry && now-lastShutdownTry<60000))return;',
     'the timer (each renewal above is bounded), a motion capture that finishes or gives up, and a 60 s retry gap'),
    ('autoPowerOff', 'if((usbAwake.inhibited(now) && !dry) || !inactivity.due(now,measuring))return;', 'as the two lines above'),
    ('autoPowerOff', 'Serial.printf("POWER_OFF_DEFERRED optical_busy refusals=%u launch=%d force_next=%d retry_s=60\\n",',
     'OpticalSleepGuard forces the next stop after FORCE_AFTER refusals (HIL sleep_blockers)'),
    ('autoPowerOff', 'if(micListener.holdsSleep(now)){leaveMicPage();return;}',
     'the Mic RPM page: leaving it stops the mic, and a stop that never ends holds the sleep MicSession::STOP_LIMIT_MS at most '
     '(test_mic_session, check_usb_pipeline, HIL sleep_blockers)'),
    ('autoPowerOff', 'if(flushed!=ShutdownFlush::Ready)return;', 'the flush exits above'),
    ('autoPowerOff', 'return;', 'a computer attached during the flush: USB power'),
    ('autoPowerOff', 'Serial.println(dry?"POWER_OFF_DEFERRED sleep_dry_run":"POWER_OFF_DEFERRED usb_host_attached_before_sleep");',
     'the HIL dry run, or a computer attached at the last moment: USB power'),
    ('autoPowerOff', 'Serial.printf("POWER_OFF_DEFERRED reason=%s restored=%d retry_s=%u\\n",dry?"sleep_dry_run":usbCancelled?"usb_host_attached":"shake_sleep_failed",restored,usbCancelled?0:60);',
     'after a power-off PM1 refused: retried every 60 s (an IMU that cannot be armed sleeps for the side key, check_usb_pipeline)'),
    ('autoPowerOff', '} else Serial.println("POWER_OFF_DEFERRED imu_bus_busy");',
     'the IMU bus busy for one attempt: retried every 60 s'),
]

FUNCTION = re.compile(r'^(?:LOOP_HELPER )?(?:static )?(?:IRAM_ATTR )?[\w:<>]+[\s*&]+(\w+)\([^;]*$')
RENEWAL = re.compile(r'inactivity\.(touch|motion|begin)\(')
EXITS = ('autoPowerOff', 'flushForShutdown')


def found(source):
    """(function, line) of every renewal, and of every exit and deferral in the sleep paths."""
    result = []
    function = None
    for line in source.splitlines():
        match = FUNCTION.match(line)
        if match and not line.startswith(' '):
            function = match.group(1)
        text = line.strip()
        if RENEWAL.search(text):
            result.append((function, text))
        elif function in EXITS and (re.search(r'\breturn\b', text) and 'return true;}' not in text or 'POWER_OFF_DEFERRED' in text):
            result.append((function, text))
    return result


def default_scenarios():
    tree = ast.parse(RUN_HIL.read_text())
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(getattr(t, 'id', None) == 'DEFAULT' for t in node.targets):
            return ast.literal_eval(node.value)
    return []


def main():
    sketch = Path(sys.argv[1]) if len(sys.argv) > 1 else SKETCH
    have = Counter(found(sketch.read_text()))
    listed = Counter((function, line) for function, line, _ in KEEP_AWAKE)
    failed = False
    for (function, line), count in sorted((have - listed).items()):
        failed = True
        print(f'NEW keep-awake path in {function} (x{count}): {line}\n'
              '  Give it a time limit that does not depend on a person, add it to KEEP_AWAKE in '
              'firmware/tests/check_keep_awake.py with that bound and its test, and a case in HIL sleep_blockers '
              'if a sensor, a store or the hardware can repeat it.')
    for (function, line), count in sorted((listed - have).items()):
        failed = True
        print(f'STALE entry in KEEP_AWAKE (x{count}), no longer in {function}: {line}\n'
              '  Remove it, or list the line that replaced it with its bound.')
    for function, line, bound in KEEP_AWAKE:
        if not bound.strip():
            failed = True
            print(f'KEEP_AWAKE entry without a bound: {function}: {line}')
    if 'sleep_blockers' not in default_scenarios():
        failed = True
        print('HIL sleep_blockers is not in run_hil.py DEFAULT, so the release gate would not run it')
    if failed:
        sys.exit(1)
    print(f'PASS: {sum(have.values())} keep-awake paths in the sketch, each listed with what ends it; HIL sleep_blockers is in the release gate')


if __name__ == '__main__':
    main()
