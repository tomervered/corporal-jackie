#!/usr/bin/env python3
"""Writes a smaller copy of ICU's data, without the parts Qt never reads.

ICU is a library of language data (sorting rules, time zones, text
encodings...). Qt Core links to it, and the data library that comes with it
is the largest file in the package: 32 MB, 12 MB of the download. Qt calls
only four groups of ICU functions (the list is printed by `objdump -p` on
Qt6Core.dll):

  sorting text (ucol_*)          uses coll/ and the locale tree
  time zones (ucal_*)            uses zone/, zoneinfo64, metaZones and the locale tree
  text encodings (ucnv_*)        uses the .cnv tables and cnvalias.icu
  upper and lower case (u_str*)  uses tables built into ICU's code

Everything below is used by none of them: word and line breaking (with its
Chinese and Thai dictionaries), the names of languages, countries,
currencies and units, spelled-out numbers, transliteration, Unicode
character names, the spoofing checks and the stringprep profiles.

Usage: trim-icu-data.py <ICU data library or .dat> <output .dat>

The script finds ICU's data inside the library (the same bytes on Windows,
macOS and Linux), writes the trimmed copy and prints the symbol name ICU
looks it up by (for example icudt78_dat). The caller wraps the copy in a
library of the same name; see the Package steps in .github/workflows/ci.yml.
"""
import struct
import sys

# Folders and files inside the data that Qt never reads (see above).
UNUSED_FOLDERS = {"brkitr", "lang", "region", "curr", "unit", "rbnf", "translit"}
UNUSED_SUFFIXES = (".spp", ".cfu")
UNUSED_FILES = {"unames.icu"}

ALIGN = 16  # ICU expects every item, and the data itself, on 16 bytes


def find_common_data(blob):
    """Offset of ICU's packaged data ("CmnD") inside a library file."""
    at = blob.find(b"CmnD")
    while at >= 0:
        start = at - 12  # 4-byte MappedData, then UDataInfo up to dataFormat
        if start >= 0 and blob[start + 2:start + 4] == b"\xda\x27":
            return start
        at = blob.find(b"CmnD", at + 1)
    raise SystemExit("no ICU common data found")


def res_size(blob, start):
    """Size of a resource bundle (.res), from its own header."""
    header = struct.unpack_from("<H", blob, start)[0]
    indexes = start + header + 4  # after the root resource word
    length = struct.unpack_from("<I", blob, indexes)[0] & 0xFF
    if length < 4:
        raise SystemExit("unexpected resource bundle layout")
    bundle_top = struct.unpack_from("<I", blob, indexes + 4 * 3)[0]
    return header + 4 * bundle_top


def read_items(blob, base):
    header = struct.unpack_from("<H", blob, base)[0]
    info = blob[base + 4:base + 4 + 20]
    if info[4] != 0:  # isBigEndian
        raise SystemExit("big-endian ICU data is not supported")
    toc = base + header
    count = struct.unpack_from("<I", blob, toc)[0]
    entries = [struct.unpack_from("<II", blob, toc + 4 + 8 * i) for i in range(count)]
    names = []
    for name_offset, _ in entries:
        end = blob.index(b"\0", toc + name_offset)
        names.append(blob[toc + name_offset:end].decode("ascii"))
    offsets = [data for _, data in entries]
    if offsets != sorted(offsets):
        raise SystemExit("ICU data items are not in offset order")
    items = []
    for i, (name, offset) in enumerate(zip(names, offsets)):
        if i + 1 < count:
            size = offsets[i + 1] - offset
        elif name.endswith(".res"):
            size = res_size(blob, toc + offset)
        else:
            raise SystemExit(f"cannot tell the size of the last item, {name}")
        items.append((name, blob[toc + offset:toc + offset + size]))
    return blob[base:base + header], items


def unused(name):
    path = name.split("/", 1)[1]  # drop the "icudt78l/" prefix
    folder, _, file = path.rpartition("/")
    return folder in UNUSED_FOLDERS or path.endswith(UNUSED_SUFFIXES) or path in UNUSED_FILES


def pad(data):
    return data + b"\0" * (-len(data) % ALIGN)


def write_common_data(header, items):
    count = len(items)
    names = b""
    name_offsets = []
    table = 4 + 8 * count
    for name, _ in items:
        name_offsets.append(table + len(names))
        names += name.encode("ascii") + b"\0"
    data_start = table + len(names)
    data_start += -data_start % ALIGN
    body = b""
    data_offsets = []
    for _, data in items:
        data_offsets.append(data_start + len(body))
        body += pad(data)
    toc = struct.pack("<I", count)
    for name_offset, data_offset in zip(name_offsets, data_offsets):
        toc += struct.pack("<II", name_offset, data_offset)
    toc += names
    toc += b"\0" * (data_start - len(toc))
    return header + toc + body


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    blob = open(sys.argv[1], "rb").read()
    base = find_common_data(blob)
    header, items = read_items(blob, base)
    kept = [(name, data) for name, data in items if not unused(name)]
    if not any(n.split("/", 1)[1].startswith("coll/") for n, _ in kept) \
            or not any(n.endswith("/zoneinfo64.res") for n, _ in kept) \
            or not any(n.endswith("/cnvalias.icu") for n, _ in kept):
        raise SystemExit("sorting, time zone or encoding data is missing; refusing to write")
    out = write_common_data(header, kept)
    open(sys.argv[2], "wb").write(out)
    before = sum(len(d) for _, d in items)
    after = sum(len(d) for _, d in kept)
    prefix = items[0][0].split("/", 1)[0]          # "icudt78l"
    symbol = prefix.rstrip("lbe") + "_dat"          # "icudt78_dat"
    print(f"ICU data: kept {len(kept)} of {len(items)} items, "
          f"{before / 1048576:.1f} MB -> {after / 1048576:.1f} MB", file=sys.stderr)
    # Bytes, so Windows Python does not add a carriage return the shell would keep.
    sys.stdout.buffer.write(symbol.encode("ascii") + b"\n")


if __name__ == "__main__":
    main()
