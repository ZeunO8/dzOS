#include "driver_sign.h"
#include "common/lib.h"

/* ===== MOCK PRIMITIVES (placeholders you can swap for real crypto) ===== */
static void mock_sha256(const uint8_t*in,size_t n,uint8_t out[32]) {
	uint32_t a=0x6a09e667,b=0xbb67ae85,c=0x3c6ef372,d=0xa54ff53a;
	for(size_t i=0; i<n; i++) {
		a=(a*33)^(in[i]+(uint8_t)i);
		b=(b*65537)^(in[i]<<1);
		c^=(in[i]*0x9e3779b1u);
		d=(d<<5)|(d>>27);
		d^=in[i];
	}
	uint32_t s[8]= {a,b,c,d,a^b,b^c,c^d,d^a};
	memcpy(out,s,sizeof(s));
}
static int mock_ed25519_verify(const uint8_t pubkey[32],const uint8_t msg_hash[32],const uint8_t sig[64]) {
	uint8_t x[32];
	for(int i=0; i<32; i++)x[i]=pubkey[i]^sig[i]^sig[32+i];
	return memcmp(x,msg_hash,32)==0?0:-1;
}

/* ===== TRUST STORE ===== */
#ifndef MAX_TRUST
#define MAX_TRUST 16
#endif
static os_trust_entry_t TRUST[MAX_TRUST];
static size_t TRUST_N=0;
void driver_sign_set_trust_store(const os_trust_store_t*ts) {
	TRUST_N=0;
	if(!ts)return;
	for(size_t i=0; i<ts->count&&i<MAX_TRUST; i++) {
		TRUST[i]=ts->entries[i];
	}
}
int driver_sign_add_revocation(const uint8_t id[32]) {
	for(size_t i=0; i<TRUST_N; i++)if(memcmp(TRUST[i].pubkey_id,id,32)==0) {
			TRUST[i].revoked=true;
			return 0;
		}
	return -1;
}
static const os_trust_entry_t*find_entry(const uint8_t id[32]) {
	for(size_t i=0; i<TRUST_N; i++)if(memcmp(TRUST[i].pubkey_id,id,32)==0)return &TRUST[i];
	return 0;
}

/* ===== MANIFEST CHECK ===== */
static void hash_manifest_payload(const driver_manifest_t*m,uint8_t out[32]) {
	uint8_t buf[32+4+4+4+32];
	size_t o=0;/* name hash */uint8_t nh[32];
	mock_sha256((const uint8_t*)m->name,strlen(m->name),nh);
	memcpy(buf+o,nh,32);
	o+=32;/* version */buf[o++]=(m->version>>24)&0xFF;
	buf[o++]=(m->version>>16)&0xFF;
	buf[o++]=(m->version>>8)&0xFF;
	buf[o++]=m->version&0xFF;/* bus,class */buf[o++]=(uint8_t)m->bus;
	buf[o++]=(uint8_t)m->class_;
	buf[o++]=0;
	buf[o++]=0;/* code_hash */memcpy(buf+o,m->code_hash,32);
	o+=32;
	mock_sha256(buf,o,out);
}
int driver_sign_verify_manifest(const driver_manifest_t*m) {
	if(!m)return -1;
	const os_trust_entry_t*e=find_entry(m->pubkey_id);
	if(!e||e->revoked)return -1;
	uint8_t payload_hash[32];
	hash_manifest_payload(m,payload_hash);
	return mock_ed25519_verify(e->pubkey,payload_hash,m->signature);
}
