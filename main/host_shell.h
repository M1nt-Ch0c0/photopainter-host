#pragma once
#include "ble_shell.h"
/* Read-only command table for signed op 28. Runs on the BLE transport worker:
 * every command is bounded, takes no admission, performs no Flash/display I/O
 * and never prints tokens, bond keys, Wi-Fi passwords or management keys. */
uint8_t host_shell_exec(int argc, char **argv, const uint8_t info[20], pp_shell_out *out);
