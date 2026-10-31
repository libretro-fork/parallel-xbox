/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <limits.h>
#define g_free free
#define g_new(type,n) ((type *)record_alloc(sizeof(type)*(n)))
#define error_report(...) ((void)0)
typedef struct VMStateField { int unused; } VMStateField;
typedef struct JSONWriter { int unused; } JSONWriter;
/* TYPES */
static uint8_t image[5*DIMM_PAGE_SIZE], fs[4*DIMM_PAGE_SIZE];
static uint64_t image_size;
static uint32_t image_crc = 0x12345678;
static bool available = true;
static size_t allocated;
static char chihiro_dimm_error[256];
static void *record_alloc(size_t size) { allocated+=size;void *p=malloc(size);assert(p||!size);return p; }
static uint8_t *chihiro_fatx_get_buffer(uint32_t *size) { *size=sizeof(fs);return fs; }
static const char *chihiro_image_basename(void) { return "synthetic-image"; }
static const uint8_t *chihiro_dimm_image_ref(uint64_t *size,uint32_t *crc,uint8_t **owned)
{ *size=image_size;*crc=image_crc;*owned=NULL;return available?image:NULL; }
/* DIMM */

/* Independent legacy v1 encoder, including the VMSTATE_UINT32 size prefix. */
static size_t legacy_encode(uint8_t *out,const uint8_t *snapshot)
{
    uint8_t base[sizeof(fs)]={0};
    memcpy(base,image,MIN(image_size,sizeof(base)));
    uint32_t count=0;
    for(unsigned pg=0;pg<4;pg++)
        if(memcmp(snapshot+pg*DIMM_PAGE_SIZE,base+pg*DIMM_PAGE_SIZE,DIMM_PAGE_SIZE))count++;
    uint32_t length=28+count*(4+DIMM_PAGE_SIZE);
    stl_be_p(out,length);
    uint32_t header[]={0x4344494d,1,(uint32_t)image_size,(uint32_t)(image_size>>32),image_crc,4096,count};
    memcpy(out+4,header,28);uint8_t *p=out+32;
    for(uint32_t pg=0;pg<4;pg++) {
        if(!memcmp(snapshot+pg*DIMM_PAGE_SIZE,base+pg*DIMM_PAGE_SIZE,DIMM_PAGE_SIZE))continue;
        memcpy(p,&pg,4);p+=4;memcpy(p,snapshot+pg*DIMM_PAGE_SIZE,DIMM_PAGE_SIZE);p+=DIMM_PAGE_SIZE;
    }
    return (size_t)(p-out);
}

int main(void)
{
    uint8_t golden[32+4*(4+DIMM_PAGE_SIZE)], output[sizeof(golden)+16], snapshot[sizeof(fs)];
    uint64_t sizes[]={1,DIMM_PAGE_SIZE-1,DIMM_PAGE_SIZE,DIMM_PAGE_SIZE+123,sizeof(image),UINT64_C(0x100000011)};
    for(unsigned i=0;i<sizeof(image);i++)image[i]=(uint8_t)(i*13u+7u);
    for(unsigned test=0;test<sizeof(sizes)/sizeof(sizes[0]);test++) for(unsigned changes=0;changes<2;changes++) {
        image_size=sizes[test];memset(fs,0,sizeof(fs));memcpy(fs,image,MIN(image_size,sizeof(fs)));
        if(changes){fs[3]^=0x56;fs[2*DIMM_PAGE_SIZE+200]^=0x89;fs[3*DIMM_PAGE_SIZE+91]^=0xef;}
        memcpy(snapshot,fs,sizeof(fs));size_t length=legacy_encode(golden,snapshot);
        ChihiroDimmMig state={0};allocated=0;assert(!chihiro_dimm_pre_save(&state));
        assert(!state.blob&&allocated==4*sizeof(uint32_t));
        assert(state.blob_size==length-4);
        QEMUFile file;memset(output,0xa5,sizeof(output));copied=0;
        qemu_file_init_output(&file,output,length);qemu_put_be32(&file,state.blob_size);
        assert(!chihiro_dimm_put_blob(&file,&state,state.blob_size,NULL,NULL));
        assert(qemu_file_transferred(&file)==length&&copied==length-4);
        assert(!memcmp(output,golden,length));
        for(size_t n=length;n<sizeof(output);n++)assert(output[n]==0xa5);
        uint8_t short_output[sizeof(output)];memset(short_output,0xa5,sizeof(short_output));
        qemu_file_init_output(&file,short_output,length-1);qemu_put_be32(&file,length-4);
        assert(chihiro_dimm_put_blob(&file,&state,length-4,NULL,NULL)<0);
        for(size_t n=length-1;n<sizeof(short_output);n++)assert(short_output[n]==0xa5);
        assert(!chihiro_dimm_post_save(&state)&&!state.changed_pages&&!state.fs);
        /* Borrow legacy bytes and restore to identical guest memory. */
        memset(fs,0xcc,sizeof(fs));copied=0;allocated=0;
        qemu_file_init_input(&file,golden,length);state.blob_size=qemu_get_be32(&file);
        assert(!chihiro_dimm_get_blob(&file,&state,state.blob_size,NULL));
        assert(state.blob==golden+4&&!copied&&!allocated);
        assert(!chihiro_dimm_post_load(&state,1)&&!state.blob&&!state.blob_size);
        assert(!memcmp(fs,snapshot,sizeof(fs))&&!memcmp(output,golden,length));
        /* A short frontend buffer fails before borrowing or writing outside it. */
        for(size_t cap=0;cap<length-4;cap++) {
            qemu_file_init_input(&file,golden+4,cap);state.blob_size=length-4;
            assert(chihiro_dimm_get_blob(&file,&state,state.blob_size,NULL)<0&&!state.blob);
        }
        /* CRC mismatch and malformed headers/page indices must be rejected. */
        for(unsigned bad=0;bad<5;bad++) {
            memcpy(output,golden,length);uint32_t value;
            if(bad<4){size_t off=4+4*(unsigned[]){0,1,4,5}[bad];memcpy(&value,output+off,4);value^=0x80;memcpy(output+off,&value,4);}
            else {if(!changes)continue;value=UINT32_MAX;memcpy(output+32,&value,4);}
            qemu_file_init_input(&file,output,length);state.blob_size=qemu_get_be32(&file);
            assert(!chihiro_dimm_get_blob(&file,&state,state.blob_size,NULL));
            assert(chihiro_dimm_post_load(&state,1)<0&&!state.blob);
        }
    }
    ChihiroDimmMig state={0};QEMUFile file;qemu_file_init_input(&file,golden,sizeof(golden));
    assert(chihiro_dimm_get_blob(&file,&state,(size_t)INT_MAX+1,NULL)<0);
    available=false;assert(chihiro_dimm_pre_save(&state)<0);
    puts("Chihiro DIMM: legacy v1 bytes, one-copy output, borrowed input, round trips, bounds and identity checks PASS");
    return 0;
}
