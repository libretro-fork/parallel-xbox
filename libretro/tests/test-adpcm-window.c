/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "hw/xbox/mcpx/apu/vp/adpcm.h"
static uint64_t state=UINT64_C(0xabc1956ef);
static uint32_t random32(void){state^=state<<13;state^=state>>7;state^=state<<17;return state>>16;}
#if defined(__GNUC__)
__attribute__((noinline,noclone))
#endif
static void full(int32_t out[64][2],const uint8_t *in,unsigned channels,unsigned start,unsigned count){
 int16_t pcm[130];assert(adpcm_decode_block(pcm,in,channels*36,channels)==65);
 for(unsigned i=0;i<count;i++)for(unsigned ch=0;ch<2;ch++)out[i][ch]=(int32_t)pcm[(start+i)*channels+(channels==1?0:ch)]*256;
}
#if defined(__GNUC__)
__attribute__((noinline,noclone))
#endif
static void window(int32_t out[64][2],const uint8_t *in,unsigned channels,unsigned start,unsigned count){assert(adpcm_decode_window(out,in,channels*36,channels,start,count)==count);}
int main(int argc,char **argv){
 uint8_t encoded[72];int32_t out[64][2];
 if(argc>1&&!strcmp(argv[1],"--bench")){
  volatile uint64_t checksum=0;
  for(unsigned channels=1;channels<=2;channels++){
   for(unsigned i=0;i<sizeof(encoded);i++)encoded[i]=random32();
   for(unsigned ch=0;ch<channels;ch++){encoded[ch*4+2]=30;encoded[ch*4+3]=0;}
   for(unsigned mode=0;mode<3;mode++)for(unsigned variant=0;variant<2;variant++){
    unsigned start=mode==0?0:mode==1?32:63,count=mode==2?1:32;
    clock_t begin=clock();
    for(unsigned n=0;n<500000;n++){
     if(variant)window(out,encoded,channels,start,count);else full(out,encoded,channels,start,count);
     checksum+=(uint32_t)out[count-1][1];
    }
    printf("ch%u_mode%u_%s=%.6f ",channels,mode,variant?"window":"full",(double)(clock()-begin)/CLOCKS_PER_SEC);
   }
  }
  printf("checksum=%llu\n",(unsigned long long)checksum);return 0;
 }
 unsigned cases=0;
 for(unsigned channels=1;channels<=2;channels++)for(unsigned trial=0;trial<89;trial++){
  for(unsigned i=0;i<sizeof(encoded);i++)encoded[i]=random32();
  for(unsigned ch=0;ch<channels;ch++){encoded[ch*4+2]=(trial*13+ch*29)%89;encoded[ch*4+3]=0;}
  uint8_t before[72];memcpy(before,encoded,sizeof(before));int16_t expected[130];
  assert(adpcm_decode_block(expected,encoded,channels*36,channels)==65);
  for(unsigned start=0;start<64;start++)for(unsigned count=0;count<=64-start;count++){
   struct {uint64_t before;int32_t pcm[64][2];uint64_t after;} guarded;
   memset(&guarded,0x35,sizeof(guarded));
   assert(adpcm_decode_window(guarded.pcm,encoded,channels*36,channels,start,count)==count);
   assert(guarded.before==UINT64_C(0x3535353535353535)&&guarded.after==UINT64_C(0x3535353535353535));
   for(unsigned i=0;i<64;i++)for(unsigned ch=0;ch<2;ch++)
    assert(guarded.pcm[i][ch]==(i<count?(int32_t)expected[(start+i)*channels+(channels==1?0:ch)]*256:0x35353535));
   assert(!memcmp(encoded,before,sizeof(before)));cases++;
  }
  for(unsigned ch=0;ch<channels;ch++)for(unsigned bad=0;bad<3;bad++){
   uint8_t saved[4];memcpy(saved,encoded+ch*4,4);
   if(bad<2)encoded[ch*4+2]=bad?255:89;else encoded[ch*4+3]=1;
   memset(out,0x35,sizeof(out));assert(!adpcm_decode_window(out,encoded,channels*36,channels,0,32));
   for(unsigned i=0;i<64;i++)for(unsigned c=0;c<2;c++)assert(out[i][c]==0x35353535);
   memcpy(encoded+ch*4,saved,4);
  }
 }
 memset(out,0x35,sizeof(out));
 assert(!adpcm_decode_window(out,NULL,0,0,0,32));
 assert(!adpcm_decode_window(out,NULL,72,3,0,32));
 assert(!adpcm_decode_window(out,NULL,35,1,0,32));
 assert(!adpcm_decode_window(out,NULL,36,1,64,0));
 assert(!adpcm_decode_window(out,NULL,36,1,63,2));
 for(unsigned i=0;i<64;i++)for(unsigned c=0;c<2;c++)assert(out[i][c]==0x35353535);
 printf("PASS: %u ADPCM windows match full decoder; mono/stereo, every start/count, malformed headers, input immutability and output canaries\n",cases);
}
