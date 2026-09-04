#!/usr/bin/env python3
"""Keep HexGrid::visitLayers() in step with HexGrid's container members.

Check mode (default): every std::vector / std::array<std::vector, N> /
std::unordered_map member of HexGrid must be visited exactly once in
HexGridLayers.hpp, and nothing else may be. Exits 1 with the difference.

    python3 scripts/check_grid_layers.py include/aoc/map/HexGrid.hpp \
        include/aoc/map/HexGridLayers.hpp

--emit prints a fresh HexGridLayers.hpp for the current member list.
"""
import re
import sys

CONTAINER = r'(?:std::vector<[^;]*?>|std::array<std::vector<[^;]*?>,\s*\d+>|std::unordered_map<[^;]*?>)'
DECL_AT_END = re.compile(CONTAINER + r'\s+(m_\w+)\s*$')
VISIT = re.compile(r'visitor\("(\w+)",\s*self\.m_(\w+)\)')


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def grid_members(path):
    """Member names in declaration order: each ';'-terminated statement that
    ends in '<container> m_name'."""
    text = strip_comments(open(path, encoding='utf-8').read())
    names = []
    for chunk in text.split(';'):
        m = DECL_AT_END.search(chunk.strip())
        if m:
            names.append(m.group(1))
    return names


def visited(path):
    return ['m_' + m.group(2) for m in VISIT.finditer(open(path, encoding='utf-8').read())]


def emit(names):
    out = []
    out.append('#pragma once')
    out.append('')
    out.append('/**')
    out.append(' * @file HexGridLayers.hpp')
    out.append(' * @brief HexGrid::visitLayers(): every container layer of the grid, by name.')
    out.append(' *')
    out.append(' * One list for both the const and the mutable visitor, so a serializer that')
    out.append(' * walks it writes and reads the same layers. `scripts/check_grid_layers.py`')
    out.append(' * (ctest `test_grid_layer_list`) fails when HexGrid gains a container member')
    out.append(' * that is not listed here: add the new layer to this list and to nothing else.')
    out.append(' */')
    out.append('')
    out.append('#include "aoc/map/HexGrid.hpp"')
    out.append('')
    out.append('namespace aoc::map {')
    out.append('')
    out.append('template <class Self, class Visitor>')
    out.append('void HexGrid::visitLayersImpl(Self& self, Visitor&& visitor) {')
    for name in names:
        out.append('    visitor("%s", self.%s);' % (name[2:], name))
    out.append('}')
    out.append('')
    out.append('template <class Visitor>')
    out.append('void HexGrid::visitLayers(Visitor&& visitor) {')
    out.append('    HexGrid::visitLayersImpl(*this, visitor);')
    out.append('}')
    out.append('')
    out.append('template <class Visitor>')
    out.append('void HexGrid::visitLayers(Visitor&& visitor) const {')
    out.append('    HexGrid::visitLayersImpl(*this, visitor);')
    out.append('}')
    out.append('')
    out.append('} // namespace aoc::map')
    return '\n'.join(out) + '\n'


def main(argv):
    if '--emit' in argv:
        argv = [a for a in argv if a != '--emit']
        sys.stdout.write(emit(grid_members(argv[1])))
        return 0
    if len(argv) != 3:
        sys.stderr.write('usage: check_grid_layers.py HexGrid.hpp HexGridLayers.hpp [--emit]\n')
        return 2
    members = grid_members(argv[1])
    listed = visited(argv[2])
    missing = [n for n in members if n not in listed]
    extra = [n for n in listed if n not in members]
    dupes = sorted({n for n in listed if listed.count(n) > 1})
    if missing or extra or dupes:
        if missing:
            sys.stderr.write('HexGridLayers.hpp is missing: %s\n' % ', '.join(missing))
        if extra:
            sys.stderr.write('HexGridLayers.hpp lists non-members: %s\n' % ', '.join(extra))
        if dupes:
            sys.stderr.write('HexGridLayers.hpp lists twice: %s\n' % ', '.join(dupes))
        return 1
    print('grid layer list in step: %d layers' % len(members))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
