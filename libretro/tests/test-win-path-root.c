/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
/* PRODUCTION */
static unsigned failures,cases;
static void check(const char *path,ptrdiff_t expected){const char*p=win_skip_path_root(path);ptrdiff_t got=p?p-path:-1;cases++;if(got!=expected){printf("root mismatch [%s] expected=%lld got=%lld\n",path,(long long)expected,(long long)got);failures++;}}
#ifdef _WIN32
typedef HRESULT (WINAPI *skip_fn)(PCWSTR,PCWSTR*);
static skip_fn old_skip;
static void native_check(const char *path){
 WCHAR w[512];assert(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,w,512));PCWSTR tail=NULL;
 HRESULT ret=old_skip(w,&tail);ptrdiff_t expected=-1;
 if(SUCCEEDED(ret))expected=WideCharToMultiByte(CP_UTF8,0,w,(int)(tail-w),NULL,0,NULL,NULL);
 check(path,expected);
}
#endif
int main(void){
 const struct{const char*p;ptrdiff_t n;} known[]={
 {"C:\\dir",3},{"C:/dir",2},{"/dir",-1},{"\\dir",1},{"C:dir",2},{"",-1},{"foo",-1},
 {"\\\\server\\share\\dir",15},{"\\\\server\\share",14},{"\\\\server",8},{"\\\\",2},
 {"\\\\?\\C:\\dir",7},{"\\\\?\\UNC\\server\\share\\dir",21},
 {"\\\\?\\Volume{01234567-89ab-cdef-0123-456789abcdef}\\dir",49},
 {"\\\\.\\C:\\dir",7},{"\\\\?\\C:",6},{"\\\\?\\UNC\\server",14},{"1:\\x",-1},
 {"c:\\",3},{"///dir",-1},{"\\\\?\\bad\\abc",-1},{"\\\\sérver\\shäre\\child",17}};
 for(unsigned i=0;i<sizeof(known)/sizeof(known[0]);i++)check(known[i].p,known[i].n);
#ifdef _WIN32
 HMODULE module=LoadLibraryA("api-ms-win-core-path-l1-1-0.dll");
 old_skip=module?(skip_fn)(void*)GetProcAddress(module,"PathCchSkipRoot"):NULL;
 if(old_skip){
  for(unsigned i=0;i<sizeof(known)/sizeof(known[0]);i++)native_check(known[i].p);
  const char*prefixes[]={"", "C:","z:\\","C:/","\\","\\\\","\\\\server\\","\\\\server\\share\\","\\\\?\\","\\\\?\\C:","\\\\?\\UNC\\","\\\\?\\unc\\","\\\\?\\Volume{01234567-89ab-cdef-0123-456789abcdef}","\\\\.\\","\\\\sérver\\shäre\\"};
  const char alphabet[]="abcXYZ0123:./\\?{}-";uint32_t seed=1;
  for(unsigned i=0;i<20000;i++){
   char path[256];strcpy(path,prefixes[i%(sizeof(prefixes)/sizeof(prefixes[0]))]);size_t n=strlen(path);
   seed=seed*1664525u+1013904223u;unsigned len=seed%64;
   for(unsigned j=0;j<len;j++){seed=seed*1664525u+1013904223u;path[n+j]=alphabet[seed%(sizeof(alphabet)-1)];}path[n+len]=0;
   native_check(path);
  }
 }
 if(module)FreeLibrary(module);
#endif
 printf("Windows path root: %u cases, %u differences\n",cases,failures);return failures?1:0;
}