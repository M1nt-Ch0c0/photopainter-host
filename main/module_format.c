#include "module_format.h"
#include <string.h>
static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return u16(p) | ((uint32_t)u16(p + 2) << 16); }
static bool range(size_t off, size_t n, size_t length)
{
    return off <= length && n <= length - off;
}
bool module_elf_valid(const uint8_t *p, size_t n)
{
    if (!p || n < 52 || n > MODULE_MAX_BYTES || memcmp(p, "\177ELF\1\1\1", 7) ||
        u16(p + 16) != 3 || u16(p + 18) != 94 || u32(p + 20) != 1 || u16(p + 40) != 52)
        return false;
    uint32_t off = u32(p + 32);
    uint16_t count = u16(p + 48), names = u16(p + 50);
    if (!count || count > 256 || u16(p + 46) != 40 || names >= count ||
        !range(off, (size_t)count * 40, n))
        return false;
    uint32_t phoff = u32(p + 28);
    uint16_t phnum = u16(p + 44);
    if (phnum && (u16(p + 42) != 32 || !range(phoff, (size_t)phnum * 32, n)))
        return false;
    const uint8_t *ns = p + off + names * 40;
    uint32_t nso = u32(ns + 16), nss = u32(ns + 20);
    if (u32(ns + 4) != 3 || !nss || !range(nso, nss, n))
        return false;
    bool entry = false, symbols = false;
    for (unsigned i = 0; i < count; i++)
    {
        const uint8_t *s = p + off + i * 40;
        uint32_t type = u32(s + 4), sz = u32(s + 20), start = u32(s + 16),
                 name = u32(s), addr = u32(s + 12);
        if (name >= nss || !memchr(p + nso + name, 0, nss - name) ||
            sz > MODULE_MAX_BYTES)
            return false;
        if (type != 8 && !range(start, sz, n))
            return false;
        if ((u32(s + 8) & 4) && u32(p + 24) >= addr &&
            (uint64_t)u32(p + 24) < (uint64_t)addr + sz)
            entry = true;
        if (type == 11)
        {
            uint32_t link = u32(s + 24);
            if (u32(s + 36) != 16 || sz % 16 || link >= count)
                return false;
            const uint8_t *strings = p + off + link * 40;
            uint32_t st = u32(strings + 16), ss = u32(strings + 20);
            if (u32(strings + 4) != 3 || !range(st, ss, n))
                return false;
            for (size_t j = 0; j < sz; j += 16)
            {
                uint32_t k = u32(p + start + j);
                if (k >= ss || !memchr(p + st + k, 0, ss - k))
                    return false;
            }
            symbols = true;
        }
        if (type == 4 || type == 9)
        {
            uint32_t es = type == 4 ? 12 : 8;
            if (u32(s + 36) != es || sz % es || u32(s + 24) >= count ||
                u32(s + 28) >= count)
                return false;
            const uint8_t *sym = p + off + u32(s + 24) * 40;
            if (u32(sym + 4) != 11 || u32(sym + 36) != 16 || u32(sym + 20) % 16)
                return false;
            for (size_t j = 0; j < sz; j += es)
            {
                uint32_t target = u32(p + start + j), info = u32(p + start + j + 4);
                if ((info >> 8) >= u32(sym + 20) / 16)
                    return false;
                if ((info & 255) == 0)
                    continue;
                bool mapped = false;
                for (unsigned k = 0; k < count; k++)
                {
                    const uint8_t *dest = p + off + k * 40;
                    uint32_t da = u32(dest + 12), ds = u32(dest + 20);
                    if ((u32(dest + 8) & 2) && ds >= 4 && target >= da &&
                        (uint64_t)target + 4 <= (uint64_t)da + ds)
                        mapped = true;
                }
                if (!mapped)
                    return false;
            }
        }
    }
    return entry && symbols;
}
