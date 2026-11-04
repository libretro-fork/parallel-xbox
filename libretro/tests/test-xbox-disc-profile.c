/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define G_FILE_TEST_IS_DIR 1
#define RETRO_VFS_FILE_ACCESS_READ 1
#define RETRO_VFS_FILE_ACCESS_HINT_NONE 0
#include "scsi/constants.h"
#define INT64_MAX_VALUE INT64_MAX
typedef struct LRFile {int unused;} LRFile;
typedef void LRQcow2;
typedef void AddressSpace;
typedef int Error;
struct retro_vfs_file_handle {int unused;};
typedef struct {void *data;size_t size;} LRFileIOV;
static LRFile file;
static struct retro_vfs_file_handle vfs_file;
static unsigned io_calls,closes,probe_calls;
static bool mapped,read_fail,found_header;
static uint64_t header_offset;
static const unsigned char magic[20]="MICROSOFT*XBOX*MEDIA";
static bool g_file_test(const char *s,int mode){return !strcmp(s,"dir");}
static bool g_str_has_prefix(const char *s,const char *p){return !strncmp(s,p,strlen(p));}
static LRFile *lr_file_open(const char *p,bool w,bool create){io_calls++;return !strcmp(p,"missing")?NULL:&file;}
static void lr_file_close(LRFile *f){closes++;}
static const uint8_t *lr_file_view(LRFile *f,int64_t off,size_t size){probe_calls++;return mapped&&!read_fail&&found_header&&(uint64_t)off==header_offset?magic:NULL;}
static int lr_file_pread(LRFile *f,int64_t off,void *buf,size_t len){io_calls++;if(read_fail)return -1;memset(buf,0,len);if(found_header&&(uint64_t)off==header_offset)memcpy(buf,magic,len);return 0;}
static struct retro_vfs_file_handle *vfs_open(const char *p,unsigned access,unsigned hints){io_calls++;return strstr(p,"missing")?NULL:&vfs_file;}
static int vfs_close(struct retro_vfs_file_handle *h){closes++;return 0;}
static struct {struct retro_vfs_file_handle *(*open)(const char *,unsigned,unsigned);int(*close)(struct retro_vfs_file_handle *);}vfs_interface={vfs_open,vfs_close},*xemu_libretro_vfs=&vfs_interface;
static int lr_vfs_readv(void *v,struct retro_vfs_file_handle *h,int64_t *cursor,int64_t off,LRFileIOV *iov,int n){assert(n==1);return lr_file_pread(NULL,off,iov->data,iov->size);}
/* TYPES */
typedef struct {bool open,locked;} Tray;
typedef struct {bool(*is_tray_open)(void*);bool(*is_medium_locked)(void*);void(*eject_request_cb)(void*,bool);void(*change_media_cb)(void*,bool,Error**);} Ops;
typedef struct {LRMedium m;bool error_logged;Ops *dev_ops;void *dev_opaque;} BlockBackend;
static Error *error_abort;
static void error_propagate(Error **e,Error *x){assert(!x);}
static void lr_qcow2_flush(void *q){}
static void lr_qcow2_close(void *q){}
static bool lr_medium_open(LRMedium *m,const char *p,uint64_t off,bool w,Error **err){io_calls++;if(!strcmp(p,"missing"))return false;memset(m,0,sizeof(*m));m->kind=LR_MEDIUM_FILE;m->file=&file;m->base=off;m->length=1024*1024;m->writable=w;return true;}
static bool tray_open(void *t){return ((Tray*)t)->open;}
static bool tray_locked(void *t){return ((Tray*)t)->locked;}
static void eject_request(void *t,bool force){assert(force);((Tray*)t)->locked=false;}
static void change_media(void *t,bool load,Error **err){((Tray*)t)->open=!load;}
static bool blk_dev_is_tray_open(BlockBackend *b){return b->dev_ops->is_tray_open(b->dev_opaque);}
/* PROBE */
/* BACKEND */
typedef struct {bool tray_open;int64_t nb_sectors;BlockBackend *blk;} IDEState;
/* ATAPI */
int main(void){
 unsigned probes=0,profiles=0;
 const uint64_t bases[]={0,0x18300000,0xfd90000,0x2080000};
 for(unsigned base=0;base<4;base++)for(unsigned view=0;view<2;view++)for(unsigned uri=0;uri<2;uri++)for(unsigned found=0;found<2;found++)for(unsigned fail=0;fail<2;fail++){
  mapped=view;found_header=found;read_fail=fail;header_offset=bases[base]+65536;
  bool identified=true;io_calls=probe_calls=closes=0;
  uint64_t offset=xbox_game_partition_offset(uri?"content://disc":"disc",&identified);
  assert(identified==(found&&!fail));assert(offset==(identified?bases[base]:0));assert(closes==1&&io_calls<=5&&probe_calls<=4);
  assert(xemu_media_is_xbox_disc(uri?"content://disc":"disc")==identified);probes++;
 }
 bool identified=true;assert(!xbox_game_partition_offset("missing",&identified)&&!identified);identified=true;assert(!xbox_game_partition_offset(NULL,&identified)&&!identified);
 assert(!xemu_media_is_xbox_disc("dir")&&!xemu_media_is_xbox_disc(NULL));
 BlockBackend b={0};
 const int64_t sizes[]={0,1,2048,CD_MAX_SECTORS-1,CD_MAX_SECTORS,CD_MAX_SECTORS+1,INT64_MAX};
 for(unsigned kind=0;kind<=LR_MEDIUM_MEMORY;kind++)for(unsigned w=0;w<2;w++)for(unsigned xbox=0;xbox<2;xbox++)for(unsigned tray=0;tray<2;tray++)for(unsigned size=0;size<ARRAY_SIZE(sizes);size++){
  b.m=(LRMedium){.kind=kind,.writable=w};lr_disk_set_xbox_disc(&b,xbox);
  bool known=xbox&&!w&&(kind==LR_MEDIUM_FILE||kind==LR_MEDIUM_VFS);
  assert(lr_disk_is_xbox_disc(&b)==known);
  IDEState s={tray,sizes[size],&b};bool present=!tray&&sizes[size]>0;
  unsigned before=io_calls;
  for(unsigned repeat=0;repeat<100;repeat++){
   bool dvd=present&&(known||sizes[size]>CD_MAX_SECTORS);
   assert(media_is_dvd(&s)==dvd&&media_is_cd(&s)==(present&&!dvd));profiles++;
  }
  assert(io_calls==before);
 }
 Tray tray={false,true};Ops ops={tray_open,tray_locked,eject_request,change_media};b=(BlockBackend){.dev_ops=&ops,.dev_opaque=&tray};
 assert(lr_disk_open(&b,"disc",0,false,NULL));lr_disk_set_xbox_disc(&b,true);
 assert(lr_disk_is_xbox_disc(&b));assert(!lr_disk_change(&b,"missing",0,NULL)&&lr_disk_is_xbox_disc(&b)&&!tray.open);
 assert(lr_disk_change(&b,"other",0,NULL)&&!lr_disk_is_xbox_disc(&b)&&!tray.open);
 lr_disk_set_xbox_disc(&b,true);assert(lr_disk_change(&b,"",0,NULL)&&tray.open&&!lr_disk_is_xbox_disc(&b));
 assert(lr_disk_change(&b,"disc",0,NULL));lr_disk_set_xbox_disc(&b,true);lr_disk_open_memory(&b,NULL,4096);assert(!lr_disk_is_xbox_disc(&b));
 lr_medium_close(&b.m);assert(!lr_disk_is_xbox_disc(&b));assert(!lr_disk_is_xbox_disc(NULL));lr_disk_set_xbox_disc(NULL,true);
 printf("PASS: %u file/VFS partition probes and %u cached ATAPI profiles; no command I/O, failed swaps, replacement/eject and memory-medium cleanup\n",probes,profiles);
}
