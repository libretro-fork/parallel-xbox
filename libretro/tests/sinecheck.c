/* Checks the audio the core gave the harness (libretro/tests/harness.c)
 * for the test ROM's tone (libretro/tests/rom/flash.asm): a 1 kHz sine of
 * amplitude 16384 at 48 kHz, 48 samples a period.
 *
 *   sinecheck <file.wav> [--frames N] [--peak P]
 *
 * From the tone's second period on (the first is the voice's resampler
 * settling), every sample must equal the one a period before (no sample
 * dropped, repeated or replaced by silence); the
 * tone must start within the first second; the file must hold N stereo
 * frames when --frames is given; and the peak must be P (default 16384,
 * the tone at 0 dB). Exit status 0 when all hold. Plain C99.
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
   long size, frames, i, first = -1, breaks = 0, want_frames = -1;
   int16_t *s;
   int peak = 0, want_peak = 16384, fail = 0;

   if (argc < 2)
   {
      fprintf(stderr, "usage: %s file.wav [--frames N] [--peak P]\n", argv[0]);
      return 2;
   }
   for (i = 2; i + 1 < argc; i += 2)
   {
      if (!strcmp(argv[i], "--frames"))
         want_frames = atol(argv[i + 1]);
      else if (!strcmp(argv[i], "--peak"))
         want_peak = atoi(argv[i + 1]);
   }

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
      fprintf(stderr, "sinecheck: not a WAV file\n");
      return 2;
   }
   frames = size / 4;
   s = (int16_t *)malloc((size_t)frames * 4 + 4);
   if (!s || fread(s, 4, (size_t)frames, f) != (size_t)frames)
   {
      fprintf(stderr, "sinecheck: short read\n");
      return 2;
   }
   fclose(f);

   for (i = 0; i < frames; i++)
   {
      int v = s[i * 2];
      if (first < 0 && (v > 100 || v < -100))
         first = i;
      if (v < 0)
         v = -v;
      if (v > peak)
         peak = v;
   }

   if (first >= 0)
      for (i = first + 2 * 48; i < frames; i++)
      {
         int d0 = s[i * 2] - s[(i - 48) * 2];
         int d1 = s[i * 2 + 1] - s[(i - 48) * 2 + 1];
         if (d0 > 64 || d0 < -64 || d1 > 64 || d1 < -64)
            breaks++;
      }

   printf("frames=%ld first=%ld peak=%d breaks=%ld\n", frames, first, peak,
         breaks);
   if (first < 0 || first > 48000)
   {
      printf("FAIL: the tone did not start within a second\n");
      fail = 1;
   }
   if (breaks)
   {
      printf("FAIL: the tone is broken at %ld samples\n", breaks);
      fail = 1;
   }
   if (want_frames >= 0 && frames != want_frames)
   {
      printf("FAIL: %ld frames, %ld expected\n", frames, want_frames);
      fail = 1;
   }
   if (peak < want_peak - 2 || peak > want_peak + 2)
   {
      printf("FAIL: peak %d, %d expected\n", peak, want_peak);
      fail = 1;
   }
   free(s);
   return fail;
}
