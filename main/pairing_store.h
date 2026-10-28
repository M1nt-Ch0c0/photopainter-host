#pragma once
#include "pairing.h"
#include "esp_err.h"
/* Only an absent namespace means factory-unbound. Existing namespace with a
 * missing/corrupt owner is a storage fault; reset persists an unbound tombstone. */
esp_err_t pp_pair_store_load(pp_pair_binding *owner,bool *bound);
bool pp_pair_store_save(const pp_pair_binding *owner);
bool pp_pair_store_clear(void);
