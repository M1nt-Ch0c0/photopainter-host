#include "ble_shell.h"
#include "ble_protocol.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void pp_shell_write(pp_shell_out *o, const char *p, size_t n) {
    size_t room = o->cap - o->len;
    if (n > room) {n = room; o->truncated = true;}
    memcpy(o->data + o->len, p, n); o->len += n;
}
void pp_shell_printf(pp_shell_out *o, const char *format, ...) {
    /* data has cap+1 bytes, so vsnprintf may always place its NUL. */
    va_list args; va_start(args, format);
    int n = vsnprintf(o->data + o->len, o->cap - o->len + 1, format, args);
    va_end(args);
    if (n < 0) {o->truncated = true; return;}
    if ((size_t)n > o->cap - o->len) {o->len = o->cap; o->truncated = true;}
    else o->len += (size_t)n;
}
bool pp_shell_command_valid(const uint8_t *p, size_t n) {
    if (!n || n > PP_SHELL_COMMAND_MAX) return false;
    for (size_t i = 0; i < n;) {
        uint8_t c = p[i];
        if (c < 0x80) {
            if ((c < 0x20 && c != '\t') || c == 0x7f) return false;
            i++; continue;
        }
        size_t extra; uint32_t v, minimum;
        if (c >= 0xc2 && c <= 0xdf) {extra = 1; v = c & 0x1f; minimum = 0x80;}
        else if (c >= 0xe0 && c <= 0xef) {extra = 2; v = c & 0x0f; minimum = 0x800;}
        else if (c >= 0xf0 && c <= 0xf4) {extra = 3; v = c & 0x07; minimum = 0x10000;}
        else return false;
        if (extra > n - i - 1) return false;
        for (size_t k = 1; k <= extra; k++) {
            if ((p[i + k] & 0xc0) != 0x80) return false;
            v = v << 6 | (p[i + k] & 0x3f);
        }
        if (v < minimum || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) return false;
        /* C1 controls are control characters too. */
        if (v >= 0x80 && v <= 0x9f) return false;
        i += extra + 1;
    }
    return true;
}
static void execute(pp_ble_shell *s, pp_shell_exec exec, const uint8_t info[20],
                    const uint8_t *p, size_t n) {
    char line[PP_SHELL_COMMAND_MAX + 1], *argv[PP_SHELL_ARGS_MAX + 1];
    int argc = 0; bool overflow = false;
    memcpy(line, p, n); line[n] = 0;
    for (char *c = line; *c;) {
        while (*c == ' ' || *c == '\t') *c++ = 0;
        if (!*c) break;
        if (argc == PP_SHELL_ARGS_MAX) {overflow = true; break;}
        argv[argc++] = c;
        while (*c && *c != ' ' && *c != '\t') c++;
    }
    argv[argc] = NULL;
    memset(s, 0, sizeof(*s));
    pp_shell_out o = {.data = s->data, .cap = PP_SHELL_OUTPUT_MAX};
    if (!argc) {pp_shell_printf(&o, "usage: empty command\n"); s->exit = PP_SHELL_EXIT_USAGE;}
    else if (overflow) {pp_shell_printf(&o, "usage: at most %d arguments\n", PP_SHELL_ARGS_MAX); s->exit = PP_SHELL_EXIT_USAGE;}
    else s->exit = exec(argc, argv, info, &o);
    if (o.len > PP_SHELL_OUTPUT_MAX) {o.len = PP_SHELL_OUTPUT_MAX; o.truncated = true;}
    s->total = (uint16_t)o.len; s->data[o.len] = 0;
    s->flags = o.truncated ? PP_SHELL_TRUNCATED : 0; s->valid = true;
}
int pp_ble_shell_request(pp_ble_shell *s, pp_shell_exec exec, const uint8_t info[20],
                         const uint8_t *p, size_t n, uint8_t *out, size_t *length) {
    *length = 0;
    if (n < 4) return PP_BLE_INVALID;
    if (!exec) return PP_BLE_STATE;
    uint32_t offset = pp_ble_u32(p);
    if (!offset) {
        if (!pp_shell_command_valid(p + 4, n - 4)) return PP_BLE_INVALID;
        execute(s, exec, info, p + 4, n - 4);
    } else {
        if (n != 4) return PP_BLE_INVALID; /* Never re-execute on a page read. */
        if (!s->valid || offset >= s->total) return PP_BLE_OFFSET;
    }
    size_t count = s->total - offset;
    if (count > PP_SHELL_PAGE_MAX) count = PP_SHELL_PAGE_MAX;
    pp_ble_put32(out, s->total); pp_ble_put32(out + 4, offset);
    out[8] = s->exit; out[9] = s->flags;
    memcpy(out + PP_SHELL_HEADER, s->data + offset, count);
    *length = PP_SHELL_HEADER + count; return PP_BLE_OK;
}
