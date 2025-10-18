#include "driver_sign.h"

/* Example: one OS public key; replace with your real keypair IDs */
static const os_trust_entry_t TRUST_ENTRIES[]={
    {.pubkey_id={0x11,0x22,0x33},.pubkey={0xAA,0xBB,0xCC},.revoked=false},
};

void os_trust_init(void) {
    static os_trust_store_t ts = {
        .entries = TRUST_ENTRIES,
        .count = sizeof(TRUST_ENTRIES) / sizeof(TRUST_ENTRIES[0])
    };
    driver_sign_set_trust_store(&ts);
}
