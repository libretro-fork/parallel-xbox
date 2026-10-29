/*
 * The DVD test's disc (rom/dvd.c): a plain image of 4096 sectors of 2048
 * bytes whose 32-bit word i of sector L is (L << 9) + i + 0x10000000,
 * little-endian, so every word read says where it came from. Sector 32
 * starts with the XDVDFS volume's magic instead, which is what makes the
 * core boot an Xbox, not a Chihiro, with it; the test reads around it.
 *
 *   mkdisc out.iso
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

int main(int argc, char **argv)
{
   FILE *f;
   uint32_t lba, i;
   unsigned char sec[2048];

   if (argc < 2 || !(f = fopen(argv[1], "wb")))
   {
      fprintf(stderr, "usage: %s out.iso\n", argv[0]);
      return 2;
   }
   for (lba = 0; lba < 4096; lba++)
   {
      for (i = 0; i < 512; i++)
      {
         uint32_t w = (lba << 9) + i + 0x10000000u;
         sec[i * 4 + 0] = (unsigned char)w;
         sec[i * 4 + 1] = (unsigned char)(w >> 8);
         sec[i * 4 + 2] = (unsigned char)(w >> 16);
         sec[i * 4 + 3] = (unsigned char)(w >> 24);
      }
      if (lba == 32)
         memcpy(sec, "MICROSOFT*XBOX*MEDIA", 20);
      if (fwrite(sec, 1, sizeof(sec), f) != sizeof(sec))
         return 1;
   }
   return fclose(f) ? 1 : 0;
}
