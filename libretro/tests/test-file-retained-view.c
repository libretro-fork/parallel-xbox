/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "block/libretro-file.h"
#include "block/libretro-read-view.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
    assert(argc==2);
    LRFile *f=lr_file_open(argv[1],false,false);assert(f);
    void *a,*b;
    const uint8_t *first=lr_file_retain_read_view(f,17,512,&a);
    const uint8_t *second=lr_file_retain_read_view(f,1024,512,&b);
    assert(first&&second&&a==b&&second==first+1007);
    uint8_t saved[1024];memcpy(saved,first,512);memcpy(saved+512,second,512);
    void *invalid=(void*)1;
    assert(!lr_file_retain_read_view(f,-1,1,&invalid)&&!invalid);
    assert(!lr_file_retain_read_view(f,lr_file_size(f),1,&invalid)&&!invalid);
    assert(!lr_file_retain_read_view(f,0,0,&invalid)&&!invalid);
    lr_file_close(f);
    assert(!memcmp(first,saved,512)&&!memcmp(second,saved+512,512));
    ((LRReadViewOwner*)a)->release(a);
    assert(!memcmp(second,saved+512,512));
    ((LRReadViewOwner*)b)->release(b);
    puts("Production libretro-common mapped file: two retained views survive close and release independently PASS");
    return 0;
}
