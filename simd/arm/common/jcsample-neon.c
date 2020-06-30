/*
 * jcsample-neon.c - downsampling (Arm NEON)
 *
 * Copyright 2020 The Chromium Authors. All Rights Reserved.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#define JPEG_INTERNALS
#include "../../../jinclude.h"
#include "../../../jpeglib.h"
#include "../../../jsimd.h"
#include "../../../jdct.h"
#include "../../../jsimddct.h"
#include "../../jsimd.h"

#include <arm_neon.h>


static const uint8_t jsimd_h2_downsample_consts[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,   /* Pad 0 */
  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,   /* Pad 1 */
  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0E,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,   /* Pad 2 */
  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0D, 0x0D,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,   /* Pad 3 */
  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0C, 0x0C, 0x0C,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,   /* Pad 4 */
  0x08, 0x09, 0x0A, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,   /* Pad 5 */
  0x08, 0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,   /* Pad 6 */
  0x08, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,   /* Pad 7 */
  0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,   /* Pad 8 */
  0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x06,   /* Pad 9 */
  0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x05, 0x05,   /* Pad 10 */
  0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x04, 0x04, 0x04,   /* Pad 11 */
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
  0x00, 0x01, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03,   /* Pad 12 */
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
  0x00, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,   /* Pad 13 */
  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
  0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,   /* Pad 14 */
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   /* Pad 15 */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


/*
 * Downsample pixel values of a single chroma component i.e. Cb, Cr.
 * This version handles the common case of 2:1 horizontal and 1:1 vertical,
 * without smoothing.
 */

void jsimd_h2v1_downsample_neon(JDIMENSION image_width,
                                int max_v_samp_factor,
                                JDIMENSION v_samp_factor,
                                JDIMENSION width_in_blocks,
                                JSAMPARRAY input_data,
                                JSAMPARRAY output_data)
{
  JSAMPROW inptr, outptr;
  /* Load expansion mask to pad remaining elements of last DCT block. */
  const int mask_offset = 16 * ((width_in_blocks * 2 * DCTSIZE) - image_width);
  const uint8x16_t expand_mask = vld1q_u8(
                                    &jsimd_h2_downsample_consts[mask_offset]);
  /* Load bias pattern alternating every pixel. */
  const uint16x8_t bias = { 0, 1, 0, 1, 0, 1, 0, 1 };

  for (unsigned outrow = 0; outrow < v_samp_factor; outrow++) {
    outptr = output_data[outrow];
    inptr = input_data[outrow];

    /* Downsample all but the last DCT block of pixels. */
    for (unsigned i = 0; i < width_in_blocks - 1; i++) {
      uint8x16_t pixels = vld1q_u8(inptr + i * 2 * DCTSIZE);
      /* Add adjacent pixel values, widen to 16-bit and add bias. */
      uint16x8_t samples_u16 = vpadalq_u8(bias, pixels);
      /* Divide total by 2 and narrow to 8-bit. */
      uint8x8_t samples_u8 = vshrn_n_u16(samples_u16, 1);
      /* Store samples to memory. */
      vst1_u8(outptr + i * DCTSIZE, samples_u8);
    }

    /* Load pixels in last DCT block into a table. */
    uint8x16_t pixels = vld1q_u8(inptr + (width_in_blocks - 1) * 2 * DCTSIZE);
#if defined(__aarch64__)
    /* Pad the empty elements with the value of the last pixel. */
    pixels = vqtbl1q_u8(pixels, expand_mask);
#else
    uint8x8x2_t table = { vget_low_u8(pixels), vget_high_u8(pixels) };
    pixels = vcombine_u8(vtbl2_u8(table, vget_low_u8(expand_mask)),
                         vtbl2_u8(table, vget_high_u8(expand_mask)));
#endif
    /* Add adjacent pixel values, widen to 16-bit and add bias. */
    uint16x8_t samples_u16 = vpadalq_u8(bias, pixels);
    /* Divide total by 2, narrow to 8-bit and store. */
    uint8x8_t samples_u8 = vshrn_n_u16(samples_u16, 1);
    vst1_u8(outptr + (width_in_blocks - 1) * DCTSIZE, samples_u8);
  }
}
