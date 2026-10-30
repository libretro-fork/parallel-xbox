/* Checks the audio the core gave the harness (libretro/tests/harness.c)
 * for the EP test ROM's output (libretro/tests/rom/ep.a56): a ramp, every
 * 16-bit sample (left and right in turn) the one before plus one,
 * wrapping at 16 bits.
 *
 *   rampcheck <file.wav> [--min N]
 *
 * From the first sample that is not 0 (the queue starts with silence), to
 * the end: no sample dropped, repeated or replaced by silence, across the
 * EP's 8-frame runs; it must start within the first second and hold at
 * least N samples (default 48000). Exit status 0 when all hold. Plain C99.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(int argc, char **argv)
{
   FILE *f;
   long size, n, i, first = -1, breaks = 0, want = 48000;
   int16_t *s;

   if (argc < 2)
   {
      fprintf(stderr, "usage: %s file.wav [--min N]\n", argv[0]);
      return 2;
   }
   if (argc > 3 && !strcmp(argv[2], "--min"))
      want = atol(argv[3]);

   f = fopen(argv[1], "rb");
   if (!f)
   {
      perror(argv[1]);
      return 2;
   }
   fseek(f, 0, SEEK_END);
   size = ftell(f) - 44;
   fseek(f, 44, SEEK_SET);
   if (size < 0)
   {
      fprintf(stderr, "rampcheck: not a WAV file\n");
      return 2;
   }
   n = size / 2;
   s = (int16_t *)malloc((size_t)n * 2 + 2);
   if (!s || fread(s, 2, (size_t)n, f) != (size_t)n)
   {
      fprintf(stderr, "rampcheck: short read\n");
      return 2;
   }
   fclose(f);

   for (i = 0; i < n; i++)
      if (s[i])
      {
         first = i;
         break;
      }
   if (first < 0 || first > 2 * 48000)
   {
      printf("FAIL: no ramp in the first second (first sample at %ld)\n", first);
      return 1;
   }
   for (i = first + 1; i < n; i++)
      if ((uint16_t)s[i] != (uint16_t)(s[i - 1] + 1))
      {
         if (breaks < 5)
            printf("break at sample %ld: %d after %d\n", i, s[i], s[i - 1]);
         breaks++;
      }
   printf("ramp: from sample %ld, %ld samples, %ld breaks\n", first,
         n - first, breaks);
   free(s);
   if (breaks || n - first < want)
   {
      printf("FAIL: the EP's output is not one unbroken ramp\n");
      return 1;
   }
   return 0;
}
