#!/usr/bin/env python3
"""The mic stays off until the Mic RPM page opens.

Mic capture holds the shared I2S clock pins and the ES8311, which the speaker
needs, takes the IMU owner's gate to start and takes heap, so nothing but the
page may reach it. An owner who never opens the page runs one line of it.

Sources are read, not run: firmware/tests/test_mic_session.cpp covers the
state machine, and the mic_page HIL scenario the device.
"""
import re
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parents[1]
SOURCES = FIRMWARE / 'LaunchLabMini'
INO = SOURCES / 'LaunchLabMini.ino'
LISTENER = 'mic_listener.h'


def after(text, marker, lines):
    head, found, tail = text.partition(marker)
    return found and '\n'.join(tail.splitlines()[:lines])


def main():
    problems = []
    sources = sorted(SOURCES.glob('*.h')) + [INO]
    for path in sources:
        text = path.read_text()
        if 'LAUNCHLAB_MIC_DIAG' in text:
            problems.append(f'{path.name}: the mic has no build flag any more')
        if path.name != LISTENER and re.search(r'\bM5\.Mic\b', text):
            problems.append(f'{path.name}: only {LISTENER} may use M5.Mic')
    ino = INO.read_text()
    if 'cfg.internal_mic=cfg.internal_rtc=false;' not in ino or 'internal_mic=true' in ino:
        problems.append('setup() must leave the mic unconfigured: the page sets it up')
    # Every call that can start the mic: the page's tick, with the page's own test.
    ticks = re.findall(r'micListener\.tick\(.*\);', ino)
    if ticks != ['micListener.tick(open,soundPlayer.powered(),now);']:
        problems.append(f'micListener.tick() must run once, from tickMicPage, got {ticks}')
    tick = after(ino, 'LOOP_HELPER static void tickMicPage() {', 4)
    if 'const bool open=controls.page==Controls::Page::MicRpm;' not in tick or 'if(!open && micListener.off())return;' not in tick:
        problems.append('tickMicPage() must stop at once unless the page is open or the mic is still running')
    if ino.count('tickMicPage();') != 1:
        problems.append('tickMicPage() must run once a loop pass')
    if ino.count('soundPlayer.muted=') != 1 or 'soundPlayer.muted=micListener.muted();' not in ino:
        problems.append('only the page may mute the speaker, from the session state')
    # A status print feeds a song in the same loop pass, with 64 ms queued:
    # the mic's lines made it longer and songs underran after a session.
    if re.findall(r'.*micListener\.status\(\);', ino) != ['  if(!soundPlayer.powered())micListener.status();']:
        problems.append('status() must leave the mic\'s lines out while the speaker is powered')
    # Sleep and power-off wait for the mic rather than meeting it, for
    # MicSession::STOP_LIMIT_MS at most (check_keep_awake.py).
    if ino.count('if(micListener.holdsSleep(now)){leaveMicPage();return;}') != 2:
        problems.append('both the inactivity sleep and the power key must leave the page and wait')
    controls = (SOURCES / 'controls.h').read_text()
    if 'Page::MicRpm' not in controls:
        problems.append('expected the Mic RPM page in controls.h: this check has lost its target')
    if 'state.page=Page::Menu;state.menu=MIC_ROW;' not in controls:
        problems.append('the mic must never start on wake: the page wakes on its menu row')
    listener = (SOURCES / LISTENER).read_text()
    # The task stays off the optical core, at the loop's priority. At the idle
    # priority a pull or a stop waited for what the loop left: the owner's
    # a8ee2234 pulls lost four spins whole and a stop took 13 s. Only the bench
    # goes down there, and a pull or a stop brings it straight back.
    if 'PRIORITY=1,BENCH_PRIORITY=0;' not in listener or re.search(r'"mic_rpm",TASK_STACK,this,PRIORITY,.*,block_,1\)', listener) is None:
        problems.append('the mic task must start on the UI core at the loop\'s priority')
    lowered = re.findall(r'vTaskPrioritySet\(nullptr,(\w+)\);', listener)
    if lowered.count('BENCH_PRIORITY') != 1 or 'vTaskPrioritySet(nullptr,BENCH_PRIORITY);\n    measure();\n    vTaskPrioritySet(nullptr,PRIORITY);' not in listener:
        problems.append('only the bench may run at the idle priority, and it must come back up')
    if listener.count('vTaskPrioritySet(task_,PRIORITY);') != 2:
        problems.append('a pull and a stop must each lift the task out of its bench')
    # A pull the ring has lapped is heard late, never dropped (a8ee2234: four of six).
    follow = after(listener, '  void follow() {', 30)
    if 'MicSession::resume(at,have,USABLE,MARGIN)' not in follow or 'overruns_;break;' in follow:
        problems.append('follow() must skip sound the ring lost, never give up the spin')
    # Two writers to the capture chain held a stop for 10 s (2e0ba398): the task lights it, once.
    start = after(listener, '  void start() {', 4)
    if start.count('queue()') != 1 or listener.count('queue()') != 4:
        problems.append('only the capture callback may refill the chain; start() lights it once')
    if 'chain_.begin();' not in listener:
        problems.append('a chain break must not outlive its session')
    # A holder the loop can preempt keeps the IMU out for a whole frame (2e0ba398).
    write = after(listener, '  bool write(uint8_t reg,uint8_t value) {', 3)
    if 'vTaskPrioritySet(nullptr,GATE_PRIORITY);' not in write:
        problems.append('a codec write must raise its priority before it takes the gate')
    # dsps_fft2r_init_fc32 takes internal heap for a reversal table the page cannot spare.
    tracker = (SOURCES / 'mic_rpm.h').read_text()
    if 'dsps_fft2r_init_fc32(' in tracker or 'esp_dsp.h' in tracker:
        problems.append('mic_rpm.h must call the FFT directly, with its table in PSRAM')
    # The public copy (firmware/SYNC.json) builds with scripts/build-firmware.sh.
    scripts = (['../scripts/build-firmware.sh'] if (FIRMWARE / 'SYNC.json').exists()
               else ['build-m5-pm.sh', 'build-m5.sh', 'build-m5-hil.sh'])
    for script in scripts:
        if 'MIC' in (FIRMWARE / script).read_text():
            problems.append(f'{script}: the mic takes no build option')
    if problems:
        raise SystemExit('\n'.join(problems))
    print('PASS: only the Mic RPM page starts the mic, and only it mutes the speaker')


if __name__ == '__main__':
    main()
