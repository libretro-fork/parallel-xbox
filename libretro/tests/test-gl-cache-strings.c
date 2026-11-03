/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Keep failed headless Windows checks out of the runtime's GUI assert path. */
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); _Exit(1); } } while (0)
/* PARSER */
#define GL_VENDOR 0
#define GL_VERSION 1
static const char *shader_gl_vendor, *shader_gl_version;
static const char *driver[2];
static unsigned queries;
static const void *glGetString(unsigned token) { queries++; return driver[token]; }
/* IDENTITY */
int main(void)
{
    uint8_t bytes[8+256];
    unsigned cases=0;
    for (unsigned len=1;len<=256;len++) {
        char expect[256];
        memset(expect,'x',len);expect[len-1]=0;
        uint64_t wire_len=len;
        memcpy(bytes,&wire_len,8);memcpy(bytes+8,expect,len);
        for (unsigned available=0;available<=8+len;available++) {
            FileCursor c={bytes,available};
            assert(cursor_take_string_eq(&c,expect)==(available==8+len));
            cases++;
        }
        /* Corruption at every byte, including the final terminator, must
         * reject a file without modifying or copying its mapped bytes. */
        for (unsigned at=0;at<len;at++) {
            bytes[8+at]^=1;
            FileCursor c={bytes,8+len};
            assert(!cursor_take_string_eq(&c,expect));
            bytes[8+at]^=1;cases++;
        }
    }
    const uint64_t bad[]={0,257,UINT64_MAX};
    for(unsigned i=0;i<3;i++) {
        memcpy(bytes,&bad[i],8);FileCursor c={bytes,sizeof(bytes)};
        assert(!cursor_take_string_eq(&c,"x"));cases++;
    }
    /* A recreated context must replace both prior GL-owned pointers,
     * including transitions into and out of either fallback string. */
    for (unsigned first=0; first<4; first++) {
        for (unsigned next=0; next<4; next++) {
            shader_gl_vendor=shader_gl_version=NULL;
            queries=0;
            driver[0]=(first&1)?NULL:"First vendor";
            driver[1]=(first&2)?NULL:"First version";
            shader_capture_gl_identity();
            assert(!strcmp(shader_gl_vendor,driver[0]?driver[0]:"Unknown vendor"));
            assert(!strcmp(shader_gl_version,driver[1]?driver[1]:"Unknown version"));
            assert(queries==2);
            driver[0]=(next&1)?NULL:"Next vendor";
            driver[1]=(next&2)?NULL:"Next version";
            shader_capture_gl_identity();
            assert(!strcmp(shader_gl_vendor,driver[0]?driver[0]:"Unknown vendor"));
            assert(!strcmp(shader_gl_version,driver[1]?driver[1]:"Unknown version"));
            assert(queries==4);
            shader_capture_gl_identity();
            assert(queries==6);
            cases++;
        }
    }
    printf("PASS: %u GL cache string cases\n",cases);
    return 0;
}
