/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define coroutine_mixed_fn
#define MIN(a,b) ((a)<(b)?(a):(b))
typedef struct Error { int tag; } Error;
typedef struct QEMUFile QEMUFile;
static Error *error_copy(Error *error) {Error *copy=malloc(sizeof(*copy));assert(copy);*copy=*error;return copy;}
static void error_free(Error *error) {free(error);}
static void error_report_err(Error *error) {error_free(error);}
static void error_propagate(Error **dest,Error *error) {if(dest)*dest=error;else error_free(error);}
static void error_setg_errno(Error **dest,int code,const char *text) {Error e={code};error_propagate(dest,error_copy(&e));}
static void stw_be_p(void *out,uint16_t value) {uint8_t *p=out;p[0]=value>>8;p[1]=value;}
static void stl_be_p(void *out,uint32_t value) {uint8_t *p=out;for(unsigned i=0;i<4;i++)p[i]=value>>(24-i*8);}
static void stq_be_p(void *out,uint64_t value) {uint8_t *p=out;for(unsigned i=0;i<8;i++)p[i]=value>>(56-i*8);}
static unsigned lduw_be_p(const void *in) {const uint8_t *p=in;return (unsigned)p[0]<<8|p[1];}
static uint32_t ldl_be_p(const void *in) {const uint8_t *p=in;uint32_t value=0;for(unsigned i=0;i<4;i++)value=value<<8|p[i];return value;}
static uint64_t ldq_be_p(const void *in) {const uint8_t *p=in;uint64_t value=0;for(unsigned i=0;i<8;i++)value=value<<8|p[i];return value;}
/* STRUCT */
static size_t copied;
static void *stream_copy(void *out,const void *in,size_t size) {copied+=size;return memcpy(out,in,size);}
#define memcpy stream_copy
/* PRODUCTION */
#undef memcpy
static void pattern(uint8_t *p,size_t n) {for(size_t i=0;i<n;i++)p[i]=(uint8_t)(i*17u+9u);}
int main(void) {
    QEMUFile file,input;
    uint8_t bytes[64];
    const uint8_t golden[]={0x12,0x34,0x89,0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0x7f};
    assert(sizeof(QEMUFile)<256);
    memset(bytes,0xa5,sizeof(bytes));qemu_file_init_output(&file,bytes,sizeof(bytes));
    qemu_put_be16(&file,0x1234);qemu_put_be32(&file,0x89abcdef);
    qemu_put_be64(&file,UINT64_C(0x0123456789abcdef));qemu_put_byte(&file,0x7f);
    assert(qemu_file_transferred(&file)==sizeof(golden)&&!copied);
    assert(!memcmp(bytes,golden,sizeof(golden))&&!qemu_fflush(&file)&&!qemu_fclose(&file));
    qemu_file_init_input(&input,bytes,sizeof(golden));
    assert(qemu_get_be16(&input)==0x1234&&qemu_get_be32(&input)==0x89abcdef);
    assert(qemu_get_be64(&input)==UINT64_C(0x0123456789abcdef)&&qemu_get_byte(&input)==0x7f);
    assert(!qemu_fclose(&input));
    /* Every truncated scalar and every short output must remain in bounds. */
    const uint8_t sequence[]={1,2,3,4,5,6,7,8};
    for(unsigned width=1;width<=8;width*=2) for(size_t cap=0;cap<=width;cap++) {
        uint64_t expected=0;
        uint64_t value=0,actual;
        for(unsigned i=0;i<width;i++) {expected=expected<<8|(i<cap?sequence[i]:0);value=value<<8|sequence[i];}
        qemu_file_init_input(&input,sequence,cap);
        switch(width) {case 1:actual=qemu_get_byte(&input);break;case 2:actual=qemu_get_be16(&input);break;case 4:actual=qemu_get_be32(&input);break;default:actual=qemu_get_be64(&input);}
        assert(actual==expected);
        assert(qemu_file_get_error(&input)==(cap<width?-EIO:0));qemu_fclose(&input);
        memset(bytes,0xa5,sizeof(bytes));qemu_file_init_output(&file,bytes,cap);
        switch(width) {case 1:qemu_put_byte(&file,value);break;case 2:qemu_put_be16(&file,value);break;case 4:qemu_put_be32(&file,value);break;default:qemu_put_be64(&file,value);}
        assert(qemu_file_get_error(&file)==(cap<width?-ENOSPC:0));
        for(size_t j=cap;j<sizeof(bytes);j++)assert(bytes[j]==0xa5);
        qemu_fclose(&file);
    }
    qemu_file_init_input(&input,sequence,sizeof(sequence));memset(bytes,0xa5,sizeof(bytes));
    assert(qemu_get_buffer(&input,bytes,9)==8&&qemu_file_get_error(&input)==-EIO);
    assert(!memcmp(bytes,sequence,8)&&bytes[8]==0xa5);qemu_fclose(&input);
    enum { LARGE=4*1024*1024 };
    uint8_t *payload=malloc(LARGE),*output=malloc(LARGE),*readback=malloc(LARGE);
    assert(payload&&output&&readback);pattern(payload,LARGE);copied=0;
    qemu_file_init_output(&file,output,LARGE);qemu_put_buffer(&file,payload,LARGE);
    assert(copied==LARGE&&qemu_file_transferred(&file)==LARGE&&!qemu_fclose(&file));
    memset(payload,0,LARGE);pattern(readback,LARGE);assert(!memcmp(output,readback,LARGE));
    qemu_file_init_input(&input,output,LARGE);
    uint8_t *view=NULL;size_t before=copied;
    assert(qemu_peek_buffer(&input,&view,LARGE-4096,4096)==LARGE-4096);
    assert(view==output+4096&&copied==before&&input.buf_index==0);
    assert(qemu_get_buffer(&input,readback,LARGE)==LARGE&&copied==before+LARGE);
    assert(!memcmp(output,readback,LARGE)&&!qemu_fclose(&input));
    qemu_file_init_input(&input,output,LARGE);
    assert(!qemu_peek_buffer(&input,&view,1,SIZE_MAX)&&!view&&qemu_file_get_error(&input)==-EIO);qemu_fclose(&input);
    qemu_file_init_input(&input,output,LARGE);qemu_file_skip(&input,-1);
    assert(input.buf_index==0&&qemu_file_get_error(&input)==-EIO);qemu_fclose(&input);
    /* An empty mapped state has no channel or scratch buffer to fall back to. */
    qemu_file_init_input(&input,NULL,0);assert(!qemu_get_byte(&input)&&qemu_file_get_error(&input)==-EIO);qemu_fclose(&input);
    qemu_file_init_output(&file,NULL,0);qemu_put_buffer(&file,NULL,0);assert(!qemu_file_get_error(&file));qemu_put_byte(&file,1);assert(qemu_fclose(&file)==-ENOSPC);
    qemu_file_init_input(&input,sequence,sizeof(sequence));qemu_put_byte(&input,0);assert(qemu_fclose(&input)==-EBADF&&!memcmp(sequence,(uint8_t[]){1,2,3,4,5,6,7,8},8));
    qemu_file_init_output(&file,bytes,sizeof(bytes));qemu_put_byte(&file,6);qemu_put_buffer(&file,(const uint8_t *)"HiXbox",6);assert(!qemu_fclose(&file));
    qemu_file_init_input(&input,bytes,7);char name[256];assert(qemu_get_counted_string(&input,name)==6&&!strcmp(name,"HiXbox"));qemu_fclose(&input);
    /* First-error ownership and repeatable finish must not leak/double-free. */
    Error first={11},second={22},*copy=NULL;
    qemu_file_init_output(&file,bytes,sizeof(bytes));qemu_file_set_error_obj(&file,-EIO,error_copy(&first));
    qemu_file_set_error_obj(&file,-ENOSPC,error_copy(&second));
    assert(qemu_file_get_error_obj_any(NULL,&file,&copy)==-EIO&&copy->tag==11);error_free(copy);
    assert(qemu_fclose(&file)==-EIO&&qemu_fclose(&file)==-EIO);
    free(payload);free(output);free(readback);
    printf("QEMUFile memory: golden bytes, borrowed views, bounds, error ownership and 4 MiB one-copy output PASS (cursor=%zu bytes)\n",sizeof(QEMUFile));
    return 0;
}
