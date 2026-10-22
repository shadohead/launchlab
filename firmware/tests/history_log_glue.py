"""The sketch's history log glue for host harnesses that compile its functions.

glue(source, functions) gives the NOR flash fake, the log's globals and the
named helper functions extracted from the sketch. Until a harness opens
`historyLog` (historyLog.open(spiffsFlash)), the log stays closed and the
sketch takes its NVS path, as before 0.18.
"""
import re
from pathlib import Path

TESTS = Path(__file__).resolve().parent

HELPERS = ('historyUnsaved', 'bestsUnsaved', 'logHistory', 'logBests')


def function(source, name):
    match = re.search(r'(?:LOOP_HELPER )?static \w+ ' + name + r'\(', source)
    start = match.start()
    cursor = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == '{') - (source[cursor] == '}')
        cursor += 1
    return source[start:cursor].replace('LOOP_HELPER ', '')


def glue(source, functions=HELPERS):
    head = (f'#include "{TESTS.parent / "LaunchLabMini/history_log.h"}"\n#include "{TESTS / "fakes/nor_flash.h"}"\n'
            'using SpiffsFlash=NorFlash;\n'
            'static SpiffsFlash spiffsFlash;static HistoryLog::Store<SpiffsFlash> historyLog;\n'
            'static uint32_t loggedHistory=0,loggedBests=0;\nstatic uint32_t powerMirror=0;\n')
    return head + '\n'.join(function(source, name) for name in functions) + '\n'


def closed(source):
    """For harnesses with their own simplified stores and Snapshot: the
    globals and the sketch's behaviour with the log closed (NVS pending, the
    log never written), as before 0.18."""
    return glue(source, ()) + ('static bool historyUnsaved(){return practiceStore.pending(practice);}\n'
                               'static bool bestsUnsaved(){return bestStore.pending(personalBests);}\n'
                               'static bool logHistory(){return false;}\nstatic bool logBests(){return false;}\n'
                               'template<class S> static void tickHistoryLog(uint32_t,const S &){}\n')
