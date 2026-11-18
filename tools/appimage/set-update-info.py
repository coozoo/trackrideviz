#!/usr/bin/env python3
"""Writes the update information into an AppImage's .upd_info section.

    python3 tools/appimage/set-update-info.py Race_Track_Visualizer-x86_64.AppImage \\
        "gh-releases-zsync|coozoo|trackrideviz|latest|Race_Track_Visualizer-x86_64.AppImage.zsync"

go-appimage embeds information that names the AppImage with its version; ours is renamed to a
name without one (settings and the like stay with the file across versions), so updaters need
the name it really has. The .zsync is made after this, from the file as it ends up.
"""

import struct
import sys


def section(data, wanted):
    """Offset and size of the named section of a 64-bit little-endian ELF."""
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        sys.exit("not a 64-bit little-endian ELF")
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    names_header = shoff + shstrndx * shentsize
    names_offset, = struct.unpack_from("<Q", data, names_header + 0x18)
    for i in range(shnum):
        header = shoff + i * shentsize
        name_index, = struct.unpack_from("<I", data, header)
        offset, size = struct.unpack_from("<QQ", data, header + 0x18)
        end = data.index(b"\0", names_offset + name_index)
        if data[names_offset + name_index:end].decode() == wanted:
            return offset, size
    sys.exit(f"no {wanted} section")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    path, information = sys.argv[1], sys.argv[2].encode()
    with open(path, "r+b") as appimage:
        # the runtime's sections are at the start, before the file system
        head = appimage.read(1024 * 1024)
        offset, size = section(head, ".upd_info")
        if len(information) >= size:
            sys.exit(f"update information longer than the section ({size} bytes)")
        appimage.seek(offset)
        appimage.write(information.ljust(size, b"\0"))
    print(f"{path}: {information.decode()}")


if __name__ == "__main__":
    main()
