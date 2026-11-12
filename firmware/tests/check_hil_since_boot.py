#!/usr/bin/env python3
"""A HIL scenario that decides a check on a figure counted from boot reboots first.

Low-water marks, maxima and counters in the status lines count from the last
boot, so in HIL `all` (and the release gate's reordered run) they hold what
every earlier scenario did. A scenario that reads one without rebooting can
pass when it runs first after a boot, as in a smoke, and fail in `all`, or
pass in `all` only because of what ran before it. 0.20.7's gate lost two
reruns to that: mic_page took the heap's since-boot low-water (an earlier
effect preview had taken it under the floor), MIC_STATUS maxima from
sleep_blockers' stuck stop, and the reprinted MOTION_GAP list.

scenarios.py lists those figures (SINCE_BOOT: status tag and field, or a
whole reprinted line) and the scenarios that read one deliberately without a
reboot, with how they guard it (SINCE_BOOT_OK). For every SCENARIOS entry this
check follows the scenario's code and the module functions it calls, in source
order, and fails on a read of a listed figure that no reboot precedes
(`reboot()`, `cut_boot()`, `!reboot` or a wait for HIL_BOOT), unless:
- it is a before/after difference through `counter()`;
- it only feeds a note or a check's description (`rig.note`, a check's name
  or detail), not whether the check passes;
- the scenario lists it in SINCE_BOOT_OK.
Each listed figure must be one the firmware prints (its printf formats), and a
field name more than one status line prints is matched only with its tag in
the same function.

  python3 firmware/tests/check_hil_since_boot.py [scenarios.py]
"""
import ast
import fnmatch
from pathlib import Path
import re
import sys

FIRMWARE = Path(__file__).resolve().parents[1]
SCENARIOS = FIRMWARE / 'tools/hil/scenarios.py'
SOURCES = FIRMWARE / 'LaunchLabMini'
REBOOTS = ('reboot', 'cut_boot')  # scenarios.py helpers that restart the unit and track the new boot
FORMAT = re.compile(r'"([A-Z][A-Z0-9_]*[A-Z0-9])((?: [^"]*)?)(?:\\n)?"')


def printed(sources=SOURCES):
    """{tag: {fields}} from the firmware's status formats ("TAG key=%u ...")."""
    tags = {}
    for path in sorted(sources.glob('*.h')) + [sources / 'LaunchLabMini.ino']:
        for tag, rest in FORMAT.findall(path.read_text()):
            tags.setdefault(tag, set()).update(re.findall(r'(\w+)=', rest))
    return tags


def literal(tree, name):
    """A module-level table of literals (and module-level string constants by name)."""
    constants = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            try:
                constants[node.targets[0].id] = ast.literal_eval(node.value)
            except ValueError:
                pass
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(getattr(t, 'id', None) == name for t in node.targets):
            names = {n.id for n in ast.walk(node.value) if isinstance(n, ast.Name)}
            if names - set(constants):
                raise SystemExit(f'scenarios.py {name} names {sorted(names - set(constants))}, which are not literals')
            return eval(compile(ast.Expression(node.value), 'scenarios.py', 'eval'), {'__builtins__': {}}, constants)
    raise SystemExit(f'scenarios.py has no {name}')


def text(node):
    """A string constant, or an f-string as a glob (`f'{kind}_min_free'` is '*_min_free')."""
    if isinstance(node, ast.Constant) and isinstance(node.value, str):
        return node.value
    if isinstance(node, ast.JoinedStr):
        return ''.join(v.value if isinstance(v, ast.Constant) else '*' for v in node.values)
    return None


class Walker:
    """Events in source order through a scenario and the module functions it calls."""

    def __init__(self, tree, figures, fields):
        self.functions = {n.name: n for n in tree.body if isinstance(n, ast.FunctionDef)}
        self.figures, self.fields = figures, fields  # [(tag, key or None)], {key: {tags}}
        self.local = {}

    @staticmethod
    def tags_in(node):
        return {t for t in (text(n) for n in ast.walk(node)) if t}

    @staticmethod
    def statement_tags(node):
        """The strings of one statement: a compound one's own header, not its body."""
        if not hasattr(node, 'body'):
            return Walker.tags_in(node)
        tags = set()
        for field, value in ast.iter_fields(node):
            if field in ('body', 'orelse', 'finalbody', 'handlers'):
                continue
            for child in value if isinstance(value, list) else [value]:
                if isinstance(child, ast.AST):
                    tags |= Walker.tags_in(child)
        return tags

    def reads(self, string, scope):
        """The listed figures a string constant names."""
        found = []
        for tag, key in self.figures:
            if key is None:
                if string.strip() == tag:
                    found.append((tag, None))
            elif (fnmatch.fnmatchcase(key, string) and len(string.replace('*', '')) >= 4) if '*' in string else string == key:
                # A field several status lines print counts only with its tag in the same statement.
                if len(self.fields.get(key, ())) == 1 or tag in scope:
                    found.append((tag, key))
        return found

    def body(self, statements, stack, quiet=False):
        return [e for stmt in statements for e in self.events(stmt, set(), stack, quiet)]

    def events(self, node, scope, stack=(), quiet=False):
        """[(kind, detail, line)]: 'reboot', or 'read' of a figure; quiet reads only describe."""
        if isinstance(node, ast.stmt):
            scope = self.statement_tags(node)
        if isinstance(node, (ast.FunctionDef, ast.Lambda)) and stack:
            if isinstance(node, ast.FunctionDef):
                self.local[node.name] = node  # its body runs where it is called
                return []
        out = []
        if isinstance(node, ast.Call):
            name = node.func.id if isinstance(node.func, ast.Name) else node.func.attr if isinstance(node.func, ast.Attribute) else None
            args = node.args
            first = text(args[0]) if args else None
            local = self.local.get(name) if isinstance(node.func, ast.Name) else None
            if name == 'counter' or (local and any(isinstance(n, ast.Call) and getattr(n.func, 'id', None) == 'counter'
                                                   for n in ast.walk(local))):
                return out  # a before/after difference of a count (or a local helper taking one)
            if isinstance(node.func, ast.Attribute) and name == 'note':
                quiet = True
            if isinstance(node.func, ast.Attribute) and name == 'check' and len(args) >= 2:
                # The name and the detail describe; only the second argument decides.
                for i, arg in enumerate(args):
                    out += self.events(arg, scope, stack, quiet or i != 1)
                for kw in node.keywords:
                    out += self.events(kw.value, scope, stack, True)
                return out
            for child in [node.func, *args, *(kw.value for kw in node.keywords)]:
                out += self.events(child, scope, stack, quiet)
            if (isinstance(node.func, ast.Attribute) and name == 'hil' and first == 'reboot') or \
                    (isinstance(node.func, ast.Attribute) and name == 'wait' and first == 'HIL_BOOT') or \
                    (isinstance(node.func, ast.Name) and name in REBOOTS):
                out.append(('reboot', name, node.lineno))
            if local is not None and name not in stack:
                out += self.body(local.body, stack + (name,), quiet)
            elif isinstance(node.func, ast.Name) and name in self.functions and name not in stack:
                out += self.body(self.functions[name].body, stack + (name,), quiet)
            return out
        string = text(node)
        if string is not None and not quiet:
            for figure in self.reads(string, scope):
                out.append(('read', figure, node.lineno))
            if isinstance(node, ast.Constant):
                return out
        for child in ast.iter_child_nodes(node):
            out += self.events(child, scope, stack, quiet)
        return out


def scenario_roots(tree):
    table = next((n.value for n in tree.body if isinstance(n, ast.Assign)
                  and any(getattr(t, 'id', None) == 'SCENARIOS' for t in n.targets)), None)
    if not isinstance(table, ast.Dict):
        raise SystemExit('scenarios.py has no SCENARIOS table')
    return [(key.value, value) for key, value in zip(table.keys, table.values)]


def check(source, tags):
    tree = ast.parse(source)
    since_boot = literal(tree, 'SINCE_BOOT')
    allowed = literal(tree, 'SINCE_BOOT_OK')
    problems = []
    figures = []
    for tag, keys in since_boot.items():
        if tag not in tags:
            problems.append(f'SINCE_BOOT: the firmware prints no {tag} line')
        if not keys:
            figures.append((tag, None))
        for key in keys:
            if key not in tags.get(tag, ()):
                problems.append(f'SINCE_BOOT: {tag} has no {key}= in the firmware\'s format')
            figures.append((tag, key))
    fields = {}
    for tag, keys in tags.items():
        for key in keys:
            fields.setdefault(key, set()).add(tag)
    walker = Walker(tree, figures, fields)
    roots = scenario_roots(tree)
    names = {name for name, _ in roots}
    for name in allowed:
        if name not in names:
            problems.append(f'SINCE_BOOT_OK names {name}, which is not in SCENARIOS')
    checked = 0
    for name, value in roots:
        fn = walker.functions.get(value.id) if isinstance(value, ast.Name) else None
        walker.local = {}
        if fn:
            events = walker.body(fn.body, (fn.name,))
        else:  # a lambda: its body expression
            events = walker.events(value.body if isinstance(value, ast.Lambda) else value, walker.tags_in(value), (name,))
        rebooted = False
        unused = set(allowed.get(name, {}))
        for kind, detail, line in events:
            if kind == 'reboot':
                rebooted = True
                continue
            tag, key = detail
            label = f'{tag} {key}' if key else tag
            if rebooted:
                continue
            checked += 1
            if label in allowed.get(name, {}):
                unused.discard(label)
                continue
            problems.append(f'{name}: scenarios.py:{line} reads {label}, which counts from boot, with no reboot first: '
                            'call reboot(rig) before it, take a before/after difference with counter(), '
                            'or list it in SINCE_BOOT_OK with how the scenario guards it')
        for label in sorted(unused):
            problems.append(f'SINCE_BOOT_OK: {name} no longer reads {label} before a reboot; remove the entry')
        for label, reason in allowed.get(name, {}).items():
            if not str(reason).strip():
                problems.append(f'SINCE_BOOT_OK: {name} {label} without a reason')
    return sorted(set(problems)), checked, len(roots)


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    path = Path(argv[0]) if argv else SCENARIOS
    problems, checked, scenarios = check(path.read_text(), printed())
    if problems:
        print('FAIL:\n' + '\n'.join(problems))
        return 1
    print(f'PASS: {scenarios} scenarios: every since-boot figure read before a reboot ({checked}) is a listed, guarded one')
    return 0


if __name__ == '__main__':
    sys.exit(main())
