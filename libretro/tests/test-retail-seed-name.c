/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
static char pgraph_game_tag[64];
static unsigned long long xemu_flip_inc_count;
static bool chihiro;
static const char *arcade_image;
static struct {struct {struct {const char *dvd_path;} files;} sys;} g_config;
static bool xemu_chihiro_mode(void){return chihiro;}
static const char *xemu_chihiro_image(void){return arcade_image;}
static int g_ascii_tolower(int c){return c>='A'&&c<='Z'?c+32:c;}
static bool g_ascii_isalnum(int c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9');}
static char *g_path_get_basename(const char *s){const char *b=s;for(const char *p=s;*p;p++)if(*p=='/'||*p==92)b=p+1;size_t n=strlen(b)+1;char *r=malloc(n);assert(r);memcpy(r,b,n);return r;}
#define g_free free
static unsigned renames;
static char old_name[64],new_name[64];
static void pgraph_seed_rename(const char *from,const char *to){renames++;snprintf(old_name,sizeof(old_name),"%s",from);snprintf(new_name,sizeof(new_name),"%s",to);}
/* PRODUCTION */
static void clear(void){memset(pgraph_game_tag,0,sizeof(pgraph_game_tag));xemu_flip_inc_count=0;renames=0;chihiro=false;arcade_image=NULL;g_config.sys.files.dvd_path=NULL;}
int main(void)
{
 clear();assert(!pgraph_seed_game_tag());g_config.sys.files.dvd_path="";assert(!pgraph_seed_game_tag());
 g_config.sys.files.dvd_path="D:/Games/Xbox/Crazy Taxi 3 - High Roller (USA).iso";
 assert(!strcmp(pgraph_seed_game_tag(),"crazy_taxi_3_-_high_roller__usa__iso"));assert(!renames);
 for(unsigned f=0;f<10000;f++){xemu_flip_inc_count=f;assert(!strcmp(pgraph_seed_game_tag(),"crazy_taxi_3_-_high_roller__usa__iso"));}
 clear();chihiro=true;arcade_image="D:/Chihiro/OUTRUN2.XBE";g_config.sys.files.dvd_path="wrong.iso";
 for(unsigned f=0;f<=900;f++){xemu_flip_inc_count=f;assert(!pgraph_seed_game_tag());}
 nv2a_set_game_name("outr2st",NULL);assert(!strcmp(pgraph_seed_game_tag(),"outr2st"));
 nv2a_set_game_name("outr2st","OUTRUN2.XBE");assert(renames==1&&!strcmp(old_name,"outrun2")&&!strcmp(new_name,"outr2st"));
 clear();chihiro=true;arcade_image="D:/Chihiro/Unknown Game.XBE";xemu_flip_inc_count=901;
 assert(!strcmp(pgraph_seed_game_tag(),"unknown_game"));
 clear();g_config.sys.files.dvd_path="D:/FOO.XBE";assert(!strcmp(pgraph_seed_game_tag(),"foo"));
 clear();chihiro=true;xemu_flip_inc_count=10000;assert(!pgraph_seed_game_tag());
 puts("PASS: retail names at startup, stable tags, absent discs, Chihiro's 900-flip grace and detected-title rename");
}
