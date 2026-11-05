#!/usr/bin/env python3
"""The USB status print feeds a playing song as it goes.

A song reaches the speaker once a loop pass with two 32 ms blocks queued
(music_stream.h). status() prints about 6 KB, 40-62 ms at USB speed, in the
same pass: on HIL (!feedgaps) the pass that printed it came within a few ms of
that, and a Mic RPM session's lines pushed it past (0.20.7). So status() calls
soundPlayer.feedSong() between groups of lines, and no group may print more
than a few lines. Sources are read, not run.
"""
import re
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parents[1]
INO = (FIRMWARE / 'LaunchLabMini/LaunchLabMini.ino').read_text()
SPEAKER = (FIRMWARE / 'LaunchLabMini/best_effect_speaker.h').read_text()
MOST_LINES = 4  # under about 700 bytes, under 10 ms at USB speed
FEEDS = 13      # one between each group of status()'s lines


def lines(segment):
    """Lines a stretch of status() can print while a song plays."""
    n = len(re.findall(r'Serial\.(?:printf|println)\(', segment))
    n += 2 * segment.count('heapStatus();') + segment.count('nvsStatus();')
    n += 2 * segment.count('soundPlayer.status(')
    # The mic's up to 14 lines count unless left out while the speaker is powered.
    if 'micListener.status();' in segment and 'if(!soundPlayer.powered())micListener.status();' not in segment:
        n += 14
    return n


def main():
    problems = []
    start = INO.index('LOOP_HELPER static void status() {')
    body = INO[start:INO.index('\n}\n', start)]
    segments = body.split('soundPlayer.feedSong();')
    if len(segments) - 1 < FEEDS:
        problems.append(f'status() feeds the song {len(segments) - 1} times, fewer than {FEEDS}')
    for i, segment in enumerate(segments):
        n = lines(segment)
        if n > MOST_LINES:
            first = re.search(r'Serial\.printf\("(\w+)', segment)
            problems.append(f'status() prints {n} lines between feeds, from {first.group(1) if first else "?"}')
    tick = SPEAKER[SPEAKER.index('  void tick(bool measuring) {'):]
    if not tick.split('\n')[1].strip() == 'feedSong();':
        problems.append('every loop pass must still feed the song first')
    if problems:
        print('FAIL: ' + '; '.join(problems))
        return 1
    print(f'PASS: status() feeds a playing song {len(segments) - 1} times, at most '
          f'{max(lines(s) for s in segments)} lines apart')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
