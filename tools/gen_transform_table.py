# Generates src/logic/transform_table.inc, the one table the universal
# transformer works from.
#
# It replaces the 49 hand written per language tables. Those had to be
# maintained by hand and disagreed with each other on what a digit meant;
# this is derived from the unicode character database, so it covers every
# latin letter unicode has and not only the ones the 49 languages happened
# to use. Vietnamese, which the old scheme could never encode, falls out
# of it for free.
#
# Run it by hand after a change to the shape family table below, and check
# the result in. It is not run at build time: the output only changes when
# this file changes, and a build that needed python would be a build that
# could break on a machine that has none.
#
# Usage:  python tools/gen_transform_table.py

import io
import re
import sys
import unicodedata as ud

# The shape family table from PROGRESS.md, keyed by the name unicode gives
# the mark. First digit is the shape family, second is the variation.
MARKS = {
    "MACRON": "1",
    "MACRON BELOW": "11",
    "STROKE": "12",
    "LOW LINE": "13",
    "ACUTE ACCENT": "2",
    "DOUBLE ACUTE ACCENT": "21",
    "ACUTE ACCENT BELOW": "22",
    "STROKE THROUGH DESCENDER": "12",
    "SOLIDUS": "23",
    "CARON": "3",
    "CARON BELOW": "32",
    "GRAVE ACCENT": "4",
    "GRAVE ACCENT BELOW": "41",
    "COMMA BELOW": "42",
    "CIRCUMFLEX ACCENT": "5",
    "CIRCUMFLEX ACCENT BELOW": "51",
    "TILDE": "6",
    "TILDE BELOW": "61",
    "BREVE": "7",
    "INVERTED BREVE": "71",
    "BREVE BELOW": "72",
    "CEDILLA": "73",
    "OGONEK": "74",
    "HORN": "75",
    "DOT ABOVE": "8",
    "DOT BELOW": "81",
    "DIAERESIS": "82",
    "DIAERESIS BELOW": "83",
    "RING ABOVE": "9",
    "RING BELOW": "91",
    "ENCLOSING CIRCLE": "92",
}

# Slot 31, the bevel, is deliberately absent: no unicode character
# decomposes into one, so nothing could ever produce it.

# The four letters that are letters in their own right rather than a base
# with a mark. They have no slot and are spelled out, which loses them --
# see the "special characters BROKEN" note in PROGRESS.md. Listed here
# rather than dropped so that a word carrying one is still readable.
# A capital one spells out as two capitals, each carrying its own case
# code, because the code applies to the letter in front of it and not to
# the word. None of these are reversible and the decoder does not try:
# see reverse_lookup() in transform.cpp for how it leaves them out.
#
# Turkish capital I with a dot is NOT here. It decomposes honestly into I
# plus a dot above, so it encodes as i0/8 and comes back whole. Spelling
# it "i0" would have made it collide with a plain capital I, which every
# other language would then have got back as a Turkish letter.
SPELLED_OUT = {
    0x00DF: "ss",     # sharp s
    0x1E9E: "s0s0",   # capital sharp s
    0x00E6: "ae",     # ae
    0x00C6: "a0e0",
    0x0153: "oe",     # oe
    0x0152: "o0e0",
    0x0131: "i",      # turkish dotless i
}

# Where latin letters live. Everything outside these is not this scheme's
# business and is dropped by the sanitizer.
RANGES = [
    (0x00C0, 0x024F),  # latin-1 supplement, extended-A, extended-B
    (0x1E00, 0x1EFF),  # extended additional, which is where vietnamese is
]


def codes_for(cp):
    """The digit codes for one codepoint, or None if it has no place here.

    Comes back as (base letter, [code, ...]) with the case code already in
    front, so "A with acute" is ("a", ["0", "2"])."""
    ch = chr(cp)
    d = ud.normalize("NFD", ch)
    base, marks = d[0], d[1:]
    out = []

    if not marks:
        # No decomposition. The unicode name still says what it is:
        # "LATIN SMALL LETTER O WITH STROKE" is an o and a stroke.
        name = ud.name(ch, "")
        m = re.match(r"LATIN (CAPITAL|SMALL) LETTER (\w) WITH (.+)$", name)
        if not m or m.group(3) not in MARKS:
            return None
        # A unicode name spells its letter in capitals whichever case the
        # character is, so the case has to come from the name saying
        # CAPITAL or SMALL and never from the letter it quotes. Reading it
        # off the letter made every such pair fold to the same thing.
        base = m.group(2) if m.group(1) == "CAPITAL" else m.group(2).lower()
        marks = ""
        out = [MARKS[m.group(3)]]

    if not ("a" <= base <= "z" or "A" <= base <= "Z"):
        return None
    for mk in marks:
        name = ud.name(mk, "").replace("COMBINING ", "")
        if name not in MARKS:
            return None
        out.append(MARKS[name])
    if not out:
        return None
    if "A" <= base <= "Z":
        out.insert(0, "0")
    return base.lower(), out


def main():
    rows = []
    for lo, hi in RANGES:
        for cp in range(lo, hi + 1):
            if cp in SPELLED_OUT:
                continue
            r = codes_for(cp)
            if not r:
                continue
            base, out = r
            rows.append((cp, base + "/".join(out)))

    for cp, text in sorted(SPELLED_OUT.items()):
        rows.append((cp, text))
    rows.sort()

    with io.open("src/logic/transform_table.inc", "w", encoding="utf-8", newline="\n") as f:
        f.write("// Generated by tools/gen_transform_table.py. Do not edit by hand.\n")
        f.write("//\n")
        f.write("// One row per latin codepoint this scheme can carry: the codepoint,\n")
        f.write("// then what it folds to. Sorted by codepoint so a lookup can bisect.\n")
        f.write("// %d rows.\n\n" % len(rows))
        f.write("// clang-format off\n")
        for cp, text in rows:
            f.write('{0x%04X, "%s"},\n' % (cp, text))
        f.write("// clang-format on\n")

    print("%d rows written to src/logic/transform_table.inc" % len(rows))

    # A fold has to be reversible, so no two codepoints may fold to the
    # same thing. This is the check that says the shape family table is
    # actually a bijection over what it covers.
    seen = {}
    clashes = 0
    for cp, text in rows:
        if text in seen:
            clashes += 1
            print("  CLASH %s: U+%04X and U+%04X" % (text, seen[text], cp))
        else:
            seen[text] = cp
    if clashes:
        print("%d clashes -- the scheme is not reversible" % clashes)
        return 1
    print("no clashes: every codepoint folds to something unique")
    return 0


if __name__ == "__main__":
    sys.exit(main())
