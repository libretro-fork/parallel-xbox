/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lrc_hash.h>
#include <retro_endianness.h>
#include <streams/file_stream.h>
/* Unused one-shot file hashing/sleep entry points must not be reached. */
RFILE *filestream_open(const char *p,unsigned m,unsigned h)
{(void)p;(void)m;(void)h;abort();}
int64_t filestream_read(RFILE *f,void *p,int64_t n)
{(void)f;(void)p;(void)n;abort();}
int64_t filestream_view(RFILE *f,int64_t n,const uint8_t **p)
{(void)f;(void)n;(void)p;abort();}
int filestream_close(RFILE *f){(void)f;abort();}
#ifdef _WIN32
void retro_sleep_us(unsigned us){(void)us;abort();}
#endif
#define cpu_to_le32 swap_if_big32
#define le32_to_cpu swap_if_big32
#define cpu_to_be32 swap_if_little32
#define qemu_fopen fopen
static void *error_fatal;
static uint32_t entropy;
static int qcrypto_random_bytes(void *p,size_t n,void *error)
{
 (void)error;uint8_t *b=p;
 for(size_t i=0;i<n;i++){entropy=entropy*1664525u+1013904223u;b[i]=(uint8_t)(entropy>>24);}
 return 0;
}
/* TYPES */
/* PRODUCTION */
int main(int argc,char **argv)
{
 assert(argc==2);uint8_t data[1024],digest[20],image[256];
 struct sha256_state summary;sha256_stream_init(&summary,0);
 for(unsigned ver=0;ver<4;ver++){
  for(unsigned len=0;len<=sizeof(data);len++){
   for(unsigned i=0;i<len;i++)data[i]=(uint8_t)(i*29+len*17+ver);
   /* CONTEXT */ ctx;
   xbox_sha1_compute(&ctx,(XboxEEPROMVersion)ver,data,len,digest);
   sha256_stream_update(&summary,digest,sizeof(digest));
  }
  for(unsigned seed=0;seed<100;seed++){
   entropy=seed+ver*997;
   assert(xbox_eeprom_generate(argv[1],(XboxEEPROMVersion)ver));
   FILE *f=fopen(argv[1],"rb");assert(f);
   assert(fread(image,1,sizeof(image),f)==sizeof(image));assert(fgetc(f)==EOF);fclose(f);
   assert(xbox_eeprom_detect_version(image)==(XboxEEPROMVersion)ver);
   sha256_stream_update(&summary,image,sizeof(image));
   image[0]^=1;assert(xbox_eeprom_detect_version(image)==(XboxEEPROMVersion)-1);
  }
 }
 uint8_t hash[32];sha256_stream_final(&summary,hash);
 assert(!memcmp(hash,"\x25\xe9\x47\xcc\xe2\x44\x12\x87\x6c\x83\xee\x2d\xf4\x0b\x06\xf1\x52\x04\xdb\xad\xf8\x38\x6a\xd8\x9b\x13\xa8\x6a\x44\xe0\x77\xf7",32));
 printf("EEPROM_SHA1=");for(unsigned i=0;i<32;i++)printf("%02x",hash[i]);puts("");
 puts("4100 custom-state hashes; 400 complete EEPROMs, version detection and tamper rejection PASS");
 return 0;
}
