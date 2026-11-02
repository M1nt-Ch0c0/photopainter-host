#pragma once
/* Read-only remote shell, signed op 28. Pure protocol side: validation, argv,
 * bounded capture and paging. No RTOS or radio here; see docs/ble-protocol.md. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PP_SHELL_COMMAND_MAX 256u
#define PP_SHELL_OUTPUT_MAX 4095u
#define PP_SHELL_PAGE_MAX 400u
#define PP_SHELL_HEADER 10u
#define PP_SHELL_ARGS_MAX 16
#define PP_SHELL_TRUNCATED 0x01u
enum {PP_SHELL_EXIT_OK=0, PP_SHELL_EXIT_ERROR=1, PP_SHELL_EXIT_USAGE=2, PP_SHELL_EXIT_UNKNOWN=127};

/* Bounded text sink. Writes past cap are dropped and set truncated. */
typedef struct pp_shell_out {char *data; size_t cap, len; bool truncated;} pp_shell_out;
void pp_shell_write(pp_shell_out *, const char *, size_t);
void pp_shell_printf(pp_shell_out *, const char *, ...) __attribute__((format(printf, 2, 3)));

/* Executes argv once; returns exit code. argc>=1. info is the 20-byte record.
 * Must be read-only, bounded, and never print secrets. */
typedef uint8_t (*pp_shell_exec)(int argc, char **argv, const uint8_t info[20], pp_shell_out *);

/* Per-session capture of the most recent exec. Zeroed by session open/close. */
typedef struct {
    char data[PP_SHELL_OUTPUT_MAX + 1];
    uint16_t total;
    uint8_t exit, flags;
    bool valid;
} pp_ble_shell;

/* 1..256 bytes of well-formed UTF-8; no NUL/control except space and tab. */
bool pp_shell_command_valid(const uint8_t *, size_t);
/* Returns a pp_ble_error. out receives the page header and data. */
int pp_ble_shell_request(pp_ble_shell *, pp_shell_exec, const uint8_t info[20],
                         const uint8_t *payload, size_t size, uint8_t *out, size_t *length);
