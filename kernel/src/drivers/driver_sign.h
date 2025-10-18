#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "driver.h"
typedef struct {
	uint8_t pubkey_id[32];
	uint8_t pubkey[32];
	bool revoked;
} os_trust_entry_t;
typedef struct {
	const os_trust_entry_t*entries;
	size_t count;
} os_trust_store_t;
void driver_sign_set_trust_store(const os_trust_store_t*ts);
int driver_sign_verify_manifest(const driver_manifest_t*m);
int driver_sign_add_revocation(const uint8_t pubkey_id[32]);
