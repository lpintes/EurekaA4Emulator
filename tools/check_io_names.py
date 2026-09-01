"""Checks src/eureka_io.h against the manual it claims to transcribe.

Every constant in that header whose comment names a source symbol -- an
IOPORT.LIB, IOREG.LIB or SYSEQU.LIB equate, or a KB.H define -- is looked up in the
matching file under eurekatech/ and compared.  A swapped mask survives both
the compiler and run-tests.bat, so this is the only thing that catches one.

Run from the repository root:  python tools/check_io_names.py
Exits non-zero if anything disagrees.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, 'src', 'eureka_io.h')
TECHMAN = os.path.join(ROOT, 'eurekatech', 'TECHMAN1')

# The libraries are 1992 DOS text: plain ASCII, but read them as latin-1 so a
# stray high byte cannot stop the check with a decode error.
LIB_ENCODING = 'latin-1'


def read_equates(*names):
    """name -> value, from the `sym equ 12h` / `sym equ 0011b` lines."""
    out = {}
    for name in names:
        path = os.path.join(TECHMAN, name)
        with open(path, encoding=LIB_ENCODING) as handle:
            for line in handle:
                line = line.split(';')[0]
                match = re.match(r'\s*(\w+)\s+equ\s+([0-9a-fA-F]+)([hb])\s*$', line)
                if match:
                    symbol, digits, base = match.groups()
                    out[symbol.lower()] = int(digits, 16 if base == 'h' else 2)
    return out


def read_kb_defines():
    """name -> value, resolving KB.H defines built out of other defines."""
    text = {}
    path = os.path.join(TECHMAN, 'KB.H')
    with open(path, encoding=LIB_ENCODING) as handle:
        for line in handle:
            match = re.match(r'#define\s+(\w+)\s+(.+?)\s*(/\*.*)?$', line.strip())
            if match:
                # KB.H defines K_AUD twice; the first wins, as it would in C.
                text.setdefault(match.group(1), match.group(2).strip())

    def value(expr, depth=0):
        if depth > 8:
            return None
        expr = expr.strip().strip('()')
        if re.fullmatch(r'0x[0-9a-fA-F]+', expr):
            return int(expr, 16)
        if re.fullmatch(r'\d+', expr):
            return int(expr)
        if '|' in expr:
            parts = [value(part, depth + 1) for part in expr.split('|')]
            if any(part is None for part in parts):
                return None
            result = 0
            for part in parts:
                result |= part
            return result
        if expr in text:
            return value(text[expr], depth + 1)
        return None

    return {name: value(expr) for name, expr in text.items()}


def read_header():
    """(constant, value, claimed source symbol) for every annotated line."""
    out = []
    with open(HEADER, encoding='utf-8') as handle:
        for line in handle:
            match = re.match(
                r'\s*constexpr \w+ (k\w+) = 0x([0-9a-f]+);\s*//\s*(\w+)', line)
            if match:
                out.append((match.group(1), int(match.group(2), 16),
                            match.group(3)))
    return out


def main():
    equates = read_equates('IOPORT.LIB', 'IOREG.LIB', 'SYSEQU.LIB')
    defines = read_kb_defines()

    checked = 0
    unmatched = 0
    problems = []
    for name, value, source in read_header():
        if source.startswith('K_'):
            expected = defines.get(source)
            where = 'KB.H'
        else:
            expected = equates.get(source.lower())
            where = 'IOPORT.LIB/IOREG.LIB/SYSEQU.LIB'
        if expected is None:
            # A comment that names something else -- prose, or a symbol from a
            # data sheet that is not in eurekatech/.  Not an error.
            unmatched += 1
            continue
        checked += 1
        if expected != value:
            problems.append(
                f'{name} = 0x{value:02x}, but {source} in {where} '
                f'is 0x{expected:02x}')

    for problem in problems:
        print('MISMATCH', problem)
    print(f'checked={checked} mismatches={len(problems)} '
          f'no_counterpart={unmatched}')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
