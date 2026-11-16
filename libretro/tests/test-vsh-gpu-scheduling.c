/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
/* PRODUCTION_SCHEDULE */
static unsigned expected_round[42512],expected_slot[42512];
static uint8_t bits[(42512+7)/8];
static void expected(unsigned first,unsigned limit,unsigned slots,unsigned rounds)
{
 for(unsigned start=first,group=0;start<limit;start+=16*slots,group++)
  for(unsigned offset=0;offset<slots;offset++)
   for(unsigned i=start+16*offset;i<start+16*(offset+1)&&i<limit;i++) {
    expected_round[i]=group%rounds;
    expected_slot[i]=(offset+group/rounds)%slots;
   }
}
int main(int argc,char **argv)
{
 if(argc==1) {
  const unsigned ranges[][2]={{0,42512},{3,100},{17,1030},{42503,42512}},round_counts[]={1,2,4,16};
  for(unsigned slots=1;slots<=4;slots++)for(unsigned n=0;n<4;n++)for(unsigned range=0;range<4;range++) {
   unsigned first=ranges[range][0],limit=ranges[range][1],rounds=round_counts[n];expected(first,limit,slots,rounds);
   for(unsigned i=first;i<limit;i++) {
    unsigned hits=0;
    for(unsigned round=0;round<rounds;round++)for(unsigned slot=0;slot<slots;slot++)
     if(fixture_case_owned(i,first,slot,slots,round,rounds)) {
      assert(expected_round[i]==round&&expected_slot[i]==slot);hits++;
     }
    assert(hits==1);
   }
  }
  puts("Vertex schedule: complete 42512-case ownership, edge ranges, 1/2/3/4 workers and 1/2/4/16 rounds PASS");return 0;
 }
 assert(argc==5);
 unsigned first=strtoul(argv[1],NULL,10),limit=strtoul(argv[2],NULL,10),slot=strtoul(argv[3],NULL,10),slots=strtoul(argv[4],NULL,10);
 unsigned round=strtoul(getenv("SPV_VSH_FIXTURE_ROUND"),NULL,10),rounds=strtoul(getenv("SPV_VSH_FIXTURE_ROUNDS"),NULL,10),count=0;
 expected(first,limit,slots,rounds);
 for(unsigned i=first;i<limit;i++)if(expected_round[i]==round&&expected_slot[i]==slot){bits[i/8]|=1u<<(i%8);count++;}
 const char *fault=getenv("VSH_SCHEDULE_FAULT");if(!fault)fault="";
 if(!strcmp(fault,"exit"))return 1;
 if(!strcmp(fault,"timeout")||!strcmp(fault,"spawn")) {
#ifdef _WIN32
  Sleep(30000);
#else
  sleep(30);
#endif
 }
 if(!strcmp(fault,"missing"))return 0;
 if(!strcmp(fault,"bitmap"))bits[0]^=1u;
 unsigned checks=count*1632;
 if(!strcmp(fault,"count"))count++;
 if(!strcmp(fault,"checks"))checks++;
 for(unsigned duplicate=0;duplicate<(!strcmp(fault,"duplicate")?2u:1u);duplicate++) {
  printf("FIXTURE_CASES count=%u checks=%u bitmap=",count,checks);
  for(unsigned i=0;i<sizeof(bits);i++)printf("%02x",bits[i]);puts("");
 }
 return 0;
}
