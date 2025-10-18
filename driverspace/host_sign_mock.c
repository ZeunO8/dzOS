/* host_sign_mock.c: demonstrate how to compute a matching mock signature */
#include <stdio.h>
// #include <string.h>
#include <stdint.h>
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
int main(int argc,char**argv) {
	(void)argc;
	(void)argv;
	uint8_t pubkey[32]= {0xAA,0xBB,0xCC},payload_hash[32],sig[64]= {0};
	uint8_t name_hash[32];
	const char*name="ps2";
	uint32_t version=1;
	uint8_t buf[32+4+4+4+32];
	size_t o=0;
	uint8_t code_hash[32]= {0x01,0x02,0x03};
	mock_sha256((const uint8_t*)name,strlen(name),name_hash);
	memcpy(buf+o,name_hash,32);
	o+=32;
	buf[o++]=(version>>24)&0xFF;
	buf[o++]=(version>>16)&0xFF;
	buf[o++]=(version>>8)&0xFF;
	buf[o++]=version&0xFF;
	buf[o++]=1;
	buf[o++]=1;
	buf[o++]=0;
	buf[o++]=0;
	memcpy(buf+o,code_hash,32);
	o+=32;
	mock_sha256(buf,o,payload_hash);
	for(int i=0; i<32; i++) {
		sig[i]=pubkey[i]^payload_hash[i];
		sig[32+i]=0;
	}
	fwrite(sig,1,64,stdout);
	return 0;
}
