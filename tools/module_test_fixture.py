"""Construct a structural ELF32/Xtensa fixture; never deploy this test payload."""

import struct


def minimal_elf():
    data = bytearray(52)
    data[:7] = b"\x7fELF\x01\x01\x01"
    sections = [(0,) * 10]
    names = b"\0.text\0.dynstr\0.dynsym\0.rela.text\0.shstrtab\0"

    def section(name, kind, flags, address, payload, link=0, info=0, entry_size=0):
        while len(data) % 4:
            data.append(0)
        offset = len(data)
        data.extend(payload)
        sections.append(
            (
                names.index(name),
                kind,
                flags,
                address,
                offset,
                len(payload),
                link,
                info,
                4,
                entry_size,
            )
        )

    section(b".text", 1, 6, 0x1000, b"\0" * 4)
    section(b".dynstr", 3, 0, 0, b"\0entry\0")
    symbol = struct.pack("<IIIBBH", 1, 0x1000, 4, 0x12, 0, 1)
    section(b".dynsym", 11, 0, 0, b"\0" * 16 + symbol, link=2, entry_size=16)
    section(
        b".rela.text",
        4,
        0,
        0,
        struct.pack("<IIi", 0x1000, 0x101, 0),
        link=3,
        info=1,
        entry_size=12,
    )
    section(b".shstrtab", 3, 0, 0, names)
    while len(data) % 4:
        data.append(0)
    section_offset = len(data)
    for item in sections:
        data.extend(struct.pack("<10I", *item))
    struct.pack_into(
        "<HHIIIIIHHHHHH",
        data,
        16,
        3,
        94,
        1,
        0x1000,
        0,
        section_offset,
        0,
        52,
        0,
        0,
        40,
        len(sections),
        5,
    )
    return bytes(data)
