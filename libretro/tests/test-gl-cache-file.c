/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#endif
typedef struct GLDiskSlot {const char *base_path;char *path,*temporary_path;size_t path_capacity;} GLDiskSlot;
static bool fail_open,fail_close,fail_rename;
static unsigned opens,renames;
static void qemu_mkdir(const char *p){
#ifdef _WIN32
 _mkdir(p);
#else
 mkdir(p,0700);
#endif
}
static FILE *qemu_fopen(const char *p,const char *mode){opens++;return fail_open?NULL:fopen(p,mode);}
static int close_file(FILE *f){int result=fclose(f);return fail_close?-1:result;}
static int g_rename(const char *a,const char *b){
 renames++;if(fail_rename)return -1;
#ifdef _WIN32
 return MoveFileExA(a,b,MOVEFILE_REPLACE_EXISTING)?0:-1;
#else
 return rename(a,b);
#endif
}
#define qemu_unlink remove
#define fclose close_file
#include "hw/xbox/nv2a/pgraph/gl/cache-file.h"
#undef fclose
static void write_pattern(FILE *f,unsigned char value,size_t size){unsigned char bytes[257];memset(bytes,value,sizeof(bytes));while(size){size_t n=size<sizeof(bytes)?size:sizeof(bytes);assert(fwrite(bytes,1,n,f)==n);size-=n;}}
static void check_file(const char *path,unsigned char value,size_t size){FILE *f=fopen(path,"rb");assert(f);for(size_t i=0;i<size;i++)assert(fgetc(f)==value);assert(fgetc(f)==EOF);assert(!fclose(f));}
static bool exists(const char *p){FILE *f=fopen(p,"rb");if(!f)return false;fclose(f);return true;}
int main(int argc,char **argv){
 assert(argc==2);
#ifndef _WIN32
 struct rlimit limit={0,0};assert(!setrlimit(RLIMIT_CORE,&limit));
#endif
 char paths[4][2048];GLDiskSlot slots[2];
 for(unsigned i=0;i<2;i++)slots[i]=(GLDiskSlot){argv[1],paths[i*2],paths[i*2+1],sizeof(paths[0])};
 for(unsigned trial=0;trial<32;trial++){
  const char *family=trial&1?"shaders_boost":"shader_modules";uint64_t hash=UINT64_C(0x1234000012340000)+trial;
  FILE *f=gl_cache_file_begin(&slots[0],family,hash);assert(f);write_pattern(f,0x63,8192);assert(gl_cache_file_finish(&slots[0],f,true));
  char published[2048];strcpy(published,slots[0].path);
#ifdef _WIN32
  HANDLE file=CreateFileA(published,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);assert(file!=INVALID_HANDLE_VALUE);
  HANDLE mapping=CreateFileMappingA(file,NULL,PAGE_READONLY,0,0,NULL);assert(mapping);
  const unsigned char *bytes=MapViewOfFile(mapping,FILE_MAP_READ,0,0,0);assert(bytes);
#else
  int fd=open(published,O_RDONLY);assert(fd>=0);const unsigned char *bytes=mmap(NULL,8192,PROT_READ,MAP_PRIVATE,fd,0);assert(bytes!=MAP_FAILED);
#endif
  /* An incomplete writer cannot replace the old file or damage its mapping. */
  f=gl_cache_file_begin(&slots[0],family,hash);assert(f);write_pattern(f,0x15,13);assert(!gl_cache_file_finish(&slots[0],f,false));assert(!exists(slots[0].temporary_path));check_file(published,0x63,8192);
  f=gl_cache_file_begin(&slots[0],family,hash);assert(f);write_pattern(f,0x26,41);fail_close=true;unsigned before=renames;assert(!gl_cache_file_finish(&slots[0],f,true)&&renames==before);fail_close=false;check_file(published,0x63,8192);assert(!exists(slots[0].temporary_path));
  f=gl_cache_file_begin(&slots[0],family,hash);assert(f);write_pattern(f,0x37,57);fail_rename=true;assert(!gl_cache_file_finish(&slots[0],f,true));fail_rename=false;check_file(published,0x63,8192);assert(!exists(slots[0].temporary_path));
  fail_open=true;assert(!gl_cache_file_begin(&slots[0],family,hash));fail_open=false;check_file(published,0x63,8192);
#ifdef _WIN32
  /* Win32 replacement may reject an actively mapped destination. Optional
   * caching must fail safely, never unlink that live old file to force it. */
  f=gl_cache_file_begin(&slots[0],family,hash);assert(f);write_pattern(f,0x48,7);
  assert(!gl_cache_file_finish(&slots[0],f,true));check_file(published,0x63,8192);
  for(unsigned i=0;i<8192;i++)assert(bytes[i]==0x63);
  UnmapViewOfFile(bytes);CloseHandle(mapping);CloseHandle(file);
#endif
  f=gl_cache_file_begin(&slots[0],family,hash);assert(f);write_pattern(f,0x48,7);
  FILE *other=gl_cache_file_begin(&slots[1],family,hash);assert(other);write_pattern(other,0x59,13001);
  assert(strcmp(slots[0].temporary_path,slots[1].temporary_path));
  assert(gl_cache_file_finish(&slots[1],other,true));check_file(published,0x59,13001);
  assert(gl_cache_file_finish(&slots[0],f,true));check_file(published,0x48,7);
  /* Touch the old final page after a much shorter replacement. Truncating
   * the original inode would SIGBUS on POSIX or corrupt the contents. */
#ifndef _WIN32
  for(unsigned i=0;i<8192;i++)assert(bytes[i]==0x63);
  pid_t child=fork();assert(child>=0);
  if(!child){for(unsigned i=0;i<24;i++){f=gl_cache_file_begin(&slots[0],family,hash);assert(f);write_pattern(f,0x6a,3001);assert(gl_cache_file_finish(&slots[0],f,true));}_exit(0);}
  for(unsigned i=0;i<24;i++){f=gl_cache_file_begin(&slots[0],family,hash);assert(f);write_pattern(f,0x7b,5009);assert(gl_cache_file_finish(&slots[0],f,true));}
  int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
  FILE *last=fopen(published,"rb");assert(last);int first=fgetc(last);fclose(last);assert(first==0x6a||first==0x7b);check_file(published,(unsigned char)first,first==0x6a?3001:5009);
  assert(!munmap((void*)bytes,8192));close(fd);
#endif
  assert(!remove(published));
 }
 unsigned before=opens;slots[0].path_capacity=2;assert(!gl_cache_file_begin(&slots[0],"shaders_boost",123)&&opens==before);
 puts("GL cache files: 32 mapped-reader replacement lifetimes, overlapping private-slot writers, incomplete/close/rename/open failures and bounded paths PASS");
#ifndef _WIN32
 puts("GL cache files: 1536 concurrent cross-process replacements remain complete; old mappings retain original bytes PASS");
#endif
}
