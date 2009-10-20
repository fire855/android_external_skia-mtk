/*
 **
 ** Copyright 2009, The Android Open Source Project
 **
 ** Licensed under the Apache License, Version 2.0 (the "License"); 
 ** you may not use this file except in compliance with the License. 
 ** You may obtain a copy of the License at 
 **
 **     http://www.apache.org/licenses/LICENSE-2.0 
 **
 ** Unless required by applicable law or agreed to in writing, software 
 ** distributed under the License is distributed on an "AS IS" BASIS, 
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
 ** See the License for the specific language governing permissions and 
 ** limitations under the License.
 */

#include <machine/cpu-features.h>
#include "SkBlitRow.h"
#include "SkColorPriv.h"
#include "SkDither.h"

#if defined(__ARM_HAVE_NEON)
#include <arm_neon.h>
#endif

#if defined(__ARM_HAVE_NEON) && defined(SK_CPU_LENDIAN)
static void S32A_D565_Opaque_neon(uint16_t* SK_RESTRICT dst,
                                  const SkPMColor* SK_RESTRICT src, int count,
                                  U8CPU alpha, int /*x*/, int /*y*/) {
    SkASSERT(255 == alpha);
    
    if (count >= 8) {
        uint16_t* SK_RESTRICT keep_dst;
        
        asm volatile (
                      "ands       ip, %[count], #7            \n\t"
                      "vmov.u8    d31, #1<<7                  \n\t"
                      "vld1.16    {q12}, [%[dst]]             \n\t"
                      "vld4.8     {d0-d3}, [%[src]]           \n\t"
                      "moveq      ip, #8                      \n\t"
                      "mov        %[keep_dst], %[dst]         \n\t"
                      
                      "add        %[src], %[src], ip, LSL#2   \n\t"
                      "add        %[dst], %[dst], ip, LSL#1   \n\t"
                      "subs       %[count], %[count], ip      \n\t"
                      "b          9f                          \n\t"
                      // LOOP
                      "2:                                         \n\t"
                      
                      "vld1.16    {q12}, [%[dst]]!            \n\t"
                      "vld4.8     {d0-d3}, [%[src]]!          \n\t"
                      "vst1.16    {q10}, [%[keep_dst]]        \n\t"
                      "sub        %[keep_dst], %[dst], #8*2   \n\t"
                      "subs       %[count], %[count], #8      \n\t"
                      "9:                                         \n\t"
                      "pld        [%[dst],#32]                \n\t"
                      // expand 0565 q12 to 8888 {d4-d7}
                      "vmovn.u16  d4, q12                     \n\t"
                      "vshr.u16   q11, q12, #5                \n\t"
                      "vshr.u16   q10, q12, #6+5              \n\t"
                      "vmovn.u16  d5, q11                     \n\t"
                      "vmovn.u16  d6, q10                     \n\t"
                      "vshl.u8    d4, d4, #3                  \n\t"
                      "vshl.u8    d5, d5, #2                  \n\t"
                      "vshl.u8    d6, d6, #3                  \n\t"
                      
                      "vmovl.u8   q14, d31                    \n\t"
                      "vmovl.u8   q13, d31                    \n\t"
                      "vmovl.u8   q12, d31                    \n\t"
                      
                      // duplicate in 4/2/1 & 8pix vsns
                      "vmvn.8     d30, d3                     \n\t"
                      "vmlal.u8   q14, d30, d6                \n\t"
                      "vmlal.u8   q13, d30, d5                \n\t"
                      "vmlal.u8   q12, d30, d4                \n\t"
                      "vshr.u16   q8, q14, #5                 \n\t"
                      "vshr.u16   q9, q13, #6                 \n\t"
                      "vaddhn.u16 d6, q14, q8                 \n\t"
                      "vshr.u16   q8, q12, #5                 \n\t"
                      "vaddhn.u16 d5, q13, q9                 \n\t"
                      "vqadd.u8   d6, d6, d0                  \n\t"  // moved up
                      "vaddhn.u16 d4, q12, q8                 \n\t"
                      // intentionally don't calculate alpha
                      // result in d4-d6
                      
                      "vqadd.u8   d5, d5, d1                  \n\t"
                      "vqadd.u8   d4, d4, d2                  \n\t"
                      
                      // pack 8888 {d4-d6} to 0565 q10
                      "vshll.u8   q10, d6, #8                 \n\t"
                      "vshll.u8   q3, d5, #8                  \n\t"
                      "vshll.u8   q2, d4, #8                  \n\t"
                      "vsri.u16   q10, q3, #5                 \n\t"
                      "vsri.u16   q10, q2, #11                \n\t"
                      
                      "bne        2b                          \n\t"
                      
                      "1:                                         \n\t"
                      "vst1.16      {q10}, [%[keep_dst]]      \n\t"
                      : [count] "+r" (count)
                      : [dst] "r" (dst), [keep_dst] "r" (keep_dst), [src] "r" (src) 
                      : "ip", "cc", "memory", "d0","d1","d2","d3","d4","d5","d6","d7",
                      "d16","d17","d18","d19","d20","d21","d22","d23","d24","d25","d26","d27","d28","d29",
                      "d30","d31"
                      );
    } else  {
        // handle count < 8
        uint16_t* SK_RESTRICT keep_dst;
        
        asm volatile (
                      "vmov.u8    d31, #1<<7                  \n\t"
                      "mov        %[keep_dst], %[dst]         \n\t"
                      
                      "tst        %[count], #4                \n\t"
                      "beq        14f                         \n\t"
                      "vld1.16    {d25}, [%[dst]]!            \n\t"
                      "vld1.32    {q1}, [%[src]]!             \n\t"
                      
                      "14:                                        \n\t"
                      "tst        %[count], #2                \n\t"
                      "beq        12f                         \n\t"
                      "vld1.32    {d24[1]}, [%[dst]]!         \n\t"
                      "vld1.32    {d1}, [%[src]]!             \n\t"
                      
                      "12:                                        \n\t"
                      "tst        %[count], #1                \n\t"
                      "beq        11f                         \n\t"
                      "vld1.16    {d24[1]}, [%[dst]]!         \n\t"
                      "vld1.32    {d0[1]}, [%[src]]!          \n\t"
                      
                      "11:                                        \n\t"
                      // unzips achieve the same as a vld4 operation
                      "vuzpq.u16  q0, q1                      \n\t"
                      "vuzp.u8    d0, d1                      \n\t"
                      "vuzp.u8    d2, d3                      \n\t"
                      // expand 0565 q12 to 8888 {d4-d7}
                      "vmovn.u16  d4, q12                     \n\t"
                      "vshr.u16   q11, q12, #5                \n\t"
                      "vshr.u16   q10, q12, #6+5              \n\t"
                      "vmovn.u16  d5, q11                     \n\t"
                      "vmovn.u16  d6, q10                     \n\t"
                      "vshl.u8    d4, d4, #3                  \n\t"
                      "vshl.u8    d5, d5, #2                  \n\t"
                      "vshl.u8    d6, d6, #3                  \n\t"
                      
                      "vmovl.u8   q14, d31                    \n\t"
                      "vmovl.u8   q13, d31                    \n\t"
                      "vmovl.u8   q12, d31                    \n\t"
                      
                      // duplicate in 4/2/1 & 8pix vsns
                      "vmvn.8     d30, d3                     \n\t"
                      "vmlal.u8   q14, d30, d6                \n\t"
                      "vmlal.u8   q13, d30, d5                \n\t"
                      "vmlal.u8   q12, d30, d4                \n\t"
                      "vshr.u16   q8, q14, #5                 \n\t"
                      "vshr.u16   q9, q13, #6                 \n\t"
                      "vaddhn.u16 d6, q14, q8                 \n\t"
                      "vshr.u16   q8, q12, #5                 \n\t"
                      "vaddhn.u16 d5, q13, q9                 \n\t"
                      "vqadd.u8   d6, d6, d0                  \n\t"  // moved up
                      "vaddhn.u16 d4, q12, q8                 \n\t"
                      // intentionally don't calculate alpha
                      // result in d4-d6
                      
                      "vqadd.u8   d5, d5, d1                  \n\t"
                      "vqadd.u8   d4, d4, d2                  \n\t"
                      
                      // pack 8888 {d4-d6} to 0565 q10
                      "vshll.u8   q10, d6, #8                 \n\t"
                      "vshll.u8   q3, d5, #8                  \n\t"
                      "vshll.u8   q2, d4, #8                  \n\t"
                      "vsri.u16   q10, q3, #5                 \n\t"
                      "vsri.u16   q10, q2, #11                \n\t"
                      
                      // store
                      "tst        %[count], #4                \n\t"
                      "beq        24f                         \n\t"
                      "vst1.16    {d21}, [%[keep_dst]]!       \n\t"
                      
                      "24:                                        \n\t"
                      "tst        %[count], #2                \n\t"
                      "beq        22f                         \n\t"
                      "vst1.32    {d20[1]}, [%[keep_dst]]!    \n\t"
                      
                      "22:                                        \n\t"
                      "tst        %[count], #1                \n\t"
                      "beq        21f                         \n\t"
                      "vst1.16    {d20[1]}, [%[keep_dst]]!    \n\t"
                      
                      "21:                                        \n\t"
                      : [count] "+r" (count)
                      : [dst] "r" (dst), [keep_dst] "r" (keep_dst), [src] "r" (src)
                      : "ip", "cc", "memory", "d0","d1","d2","d3","d4","d5","d6","d7",
                      "d16","d17","d18","d19","d20","d21","d22","d23","d24","d25","d26","d27","d28","d29",
                      "d30","d31"
                      );
    }
}

static void S32A_D565_Blend_neon(uint16_t* SK_RESTRICT dst,
                                 const SkPMColor* SK_RESTRICT src, int count,
                                 U8CPU alpha, int /*x*/, int /*y*/) {
    asm volatile (
    /* This code implements a Neon version of S32A_D565_Blend. The output differs from
     * the original in two respects:
     *  1. The results have a few mismatches compared to the original code. These mismatches
     *     never exceed 1. It's possible to improve accuracy vs. a floating point
     *     implementation by introducing rounding right shifts (vrshr) for the final stage.
     *     Rounding is not present in the code below, because although results would be closer
     *     to a floating point implementation, the number of mismatches compared to the 
     *     original code would be far greater.
     *  2. On certain inputs, the original code can overflow, causing colour channels to
     *     mix. Although the Neon code can also overflow, it doesn't allow one colour channel
     *     to affect another.
     */
                  
                  "add        %[alpha], %[alpha], %[alpha], lsr #7    \n\t"   // adjust range of alpha 0-256
                  "vmov.u16   q3, #255                        \n\t"   // set up constant
                  "movs       r4, %[count], lsr #3            \n\t"   // calc. count>>3
                  "vmov.u16   d2[0], %[alpha]                 \n\t"   // move alpha to Neon
                  "beq        2f                              \n\t"   // if count8 == 0, exit
                  "vmov.u16   q15, #0x1f                      \n\t"   // set up blue mask
                  
                  "1:                                             \n\t"
                  "vld1.u16   {d0, d1}, [%[dst]]              \n\t"   // load eight dst RGB565 pixels
                  "subs       r4, r4, #1                      \n\t"   // decrement loop counter
                  "vld4.u8    {d24, d25, d26, d27}, [%[src]]! \n\t"   // load eight src ABGR32 pixels
                  //  and deinterleave
                  
                  "vshl.u16   q9, q0, #5                      \n\t"   // shift green to top of lanes
                  "vand       q10, q0, q15                    \n\t"   // extract blue
                  "vshr.u16   q8, q0, #11                     \n\t"   // extract red
                  "vshr.u16   q9, q9, #10                     \n\t"   // extract green
                  // dstrgb = {q8, q9, q10}
                  
                  "vshr.u8    d24, d24, #3                    \n\t"   // shift red to 565 range
                  "vshr.u8    d25, d25, #2                    \n\t"   // shift green to 565 range
                  "vshr.u8    d26, d26, #3                    \n\t"   // shift blue to 565 range
                  
                  "vmovl.u8   q11, d24                        \n\t"   // widen red to 16 bits
                  "vmovl.u8   q12, d25                        \n\t"   // widen green to 16 bits
                  "vmovl.u8   q14, d27                        \n\t"   // widen alpha to 16 bits
                  "vmovl.u8   q13, d26                        \n\t"   // widen blue to 16 bits
                  // srcrgba = {q11, q12, q13, q14}
                  
                  "vmul.u16   q2, q14, d2[0]                  \n\t"   // sa * src_scale
                  "vmul.u16   q11, q11, d2[0]                 \n\t"   // red result = src_red * src_scale
                  "vmul.u16   q12, q12, d2[0]                 \n\t"   // grn result = src_grn * src_scale
                  "vmul.u16   q13, q13, d2[0]                 \n\t"   // blu result = src_blu * src_scale
                  
                  "vshr.u16   q2, q2, #8                      \n\t"   // sa * src_scale >> 8
                  "vsub.u16   q2, q3, q2                      \n\t"   // 255 - (sa * src_scale >> 8)
                  // dst_scale = q2
                  
                  "vmla.u16   q11, q8, q2                     \n\t"   // red result += dst_red * dst_scale
                  "vmla.u16   q12, q9, q2                     \n\t"   // grn result += dst_grn * dst_scale
                  "vmla.u16   q13, q10, q2                    \n\t"   // blu result += dst_blu * dst_scale
                  
                  "vshr.u16   q11, q11, #8                    \n\t"   // shift down red
                  "vshr.u16   q12, q12, #8                    \n\t"   // shift down green
                  "vshr.u16   q13, q13, #8                    \n\t"   // shift down blue
                  
                  "vsli.u16   q13, q12, #5                    \n\t"   // insert green into blue
                  "vsli.u16   q13, q11, #11                   \n\t"   // insert red into green/blue
                  "vst1.16    {d26, d27}, [%[dst]]!           \n\t"   // write pixel back to dst, update ptr
                  
                  "bne        1b                              \n\t"   // if counter != 0, loop
                  "2:                                             \n\t"   // exit
                  
                  : [src] "+r" (src), [dst] "+r" (dst), [count] "+r" (count), [alpha] "+r" (alpha)
                  :
                  : "cc", "memory", "r4", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31"
                  );
    
    count &= 7;
    if (count > 0) {
        do {
            SkPMColor sc = *src++;
            if (sc)
            {
                uint16_t dc = *dst;
                unsigned sa = SkGetPackedA32(sc);
                unsigned dr, dg, db;
                
                if (sa == 255) {
                    dr = SkAlphaBlend(SkPacked32ToR16(sc), SkGetPackedR16(dc), alpha);
                    dg = SkAlphaBlend(SkPacked32ToG16(sc), SkGetPackedG16(dc), alpha);
                    db = SkAlphaBlend(SkPacked32ToB16(sc), SkGetPackedB16(dc), alpha);
                } else {
                    unsigned dst_scale = 255 - SkAlphaMul(sa, alpha);
                    dr = (SkPacked32ToR16(sc) * alpha + SkGetPackedR16(dc) * dst_scale) >> 8;
                    dg = (SkPacked32ToG16(sc) * alpha + SkGetPackedG16(dc) * dst_scale) >> 8;
                    db = (SkPacked32ToB16(sc) * alpha + SkGetPackedB16(dc) * dst_scale) >> 8;
                }
                *dst = SkPackRGB16(dr, dg, db);
            }
            dst += 1;
        } while (--count != 0);
    }
}

/* dither matrix for Neon, derived from gDitherMatrix_3Bit_16.
 * each dither value is spaced out into byte lanes, and repeated
 * to allow an 8-byte load from offsets 0, 1, 2 or 3 from the
 * start of each row.
 */
static const uint8_t gDitherMatrix_Neon[48] = {
    0, 4, 1, 5, 0, 4, 1, 5, 0, 4, 1, 5,
    6, 2, 7, 3, 6, 2, 7, 3, 6, 2, 7, 3,
    1, 5, 0, 4, 1, 5, 0, 4, 1, 5, 0, 4,
    7, 3, 6, 2, 7, 3, 6, 2, 7, 3, 6, 2,
    
};

static void S32_D565_Blend_Dither_neon(uint16_t *dst, const SkPMColor *src,
                                       int count, U8CPU alpha, int x, int y)
{
    /* select row and offset for dither array */
    const uint8_t *dstart = &gDitherMatrix_Neon[(y&3)*12 + (x&3)];
    
    /* rescale alpha to range 0 - 256 */
    int scale = SkAlpha255To256(alpha);
    
    asm volatile (
                  "vld1.8         {d31}, [%[dstart]]              \n\t"   // load dither values
                  "vshr.u8        d30, d31, #1                    \n\t"   // calc. green dither values
                  "vdup.16        d6, %[scale]                    \n\t"   // duplicate scale into neon reg
                  "vmov.i8        d29, #0x3f                      \n\t"   // set up green mask
                  "vmov.i8        d28, #0x1f                      \n\t"   // set up blue mask
                  "1:                                                 \n\t"
                  "vld4.8         {d0, d1, d2, d3}, [%[src]]!     \n\t"   // load 8 pixels and split into argb
                  "vshr.u8        d22, d0, #5                     \n\t"   // calc. red >> 5
                  "vshr.u8        d23, d1, #6                     \n\t"   // calc. green >> 6
                  "vshr.u8        d24, d2, #5                     \n\t"   // calc. blue >> 5
                  "vaddl.u8       q8, d0, d31                     \n\t"   // add in dither to red and widen
                  "vaddl.u8       q9, d1, d30                     \n\t"   // add in dither to green and widen
                  "vaddl.u8       q10, d2, d31                    \n\t"   // add in dither to blue and widen
                  "vsubw.u8       q8, q8, d22                     \n\t"   // sub shifted red from result
                  "vsubw.u8       q9, q9, d23                     \n\t"   // sub shifted green from result
                  "vsubw.u8       q10, q10, d24                   \n\t"   // sub shifted blue from result
                  "vshrn.i16      d22, q8, #3                     \n\t"   // shift right and narrow to 5 bits
                  "vshrn.i16      d23, q9, #2                     \n\t"   // shift right and narrow to 6 bits
                  "vshrn.i16      d24, q10, #3                    \n\t"   // shift right and narrow to 5 bits
                  // load 8 pixels from dst, extract rgb
                  "vld1.16        {d0, d1}, [%[dst]]              \n\t"   // load 8 pixels
                  "vshrn.i16      d17, q0, #5                     \n\t"   // shift green down to bottom 6 bits
                  "vmovn.i16      d18, q0                         \n\t"   // narrow to get blue as bytes
                  "vshr.u16       q0, q0, #11                     \n\t"   // shift down to extract red
                  "vand           d17, d17, d29                   \n\t"   // and green with green mask
                  "vand           d18, d18, d28                   \n\t"   // and blue with blue mask
                  "vmovn.i16      d16, q0                         \n\t"   // narrow to get red as bytes
                  // src = {d22 (r), d23 (g), d24 (b)}
                  // dst = {d16 (r), d17 (g), d18 (b)}
                  // subtract dst from src and widen
                  "vsubl.s8       q0, d22, d16                    \n\t"   // subtract red src from dst
                  "vsubl.s8       q1, d23, d17                    \n\t"   // subtract green src from dst
                  "vsubl.s8       q2, d24, d18                    \n\t"   // subtract blue src from dst
                  // multiply diffs by scale and shift
                  "vmul.i16       q0, q0, d6[0]                   \n\t"   // multiply red by scale
                  "vmul.i16       q1, q1, d6[0]                   \n\t"   // multiply blue by scale
                  "vmul.i16       q2, q2, d6[0]                   \n\t"   // multiply green by scale
                  "subs           %[count], %[count], #8          \n\t"   // decrement loop counter
                  "vshrn.i16      d0, q0, #8                      \n\t"   // shift down red by 8 and narrow
                  "vshrn.i16      d2, q1, #8                      \n\t"   // shift down green by 8 and narrow
                  "vshrn.i16      d4, q2, #8                      \n\t"   // shift down blue by 8 and narrow
                  // add dst to result
                  "vaddl.s8       q0, d0, d16                     \n\t"   // add dst to red
                  "vaddl.s8       q1, d2, d17                     \n\t"   // add dst to green
                  "vaddl.s8       q2, d4, d18                     \n\t"   // add dst to blue
                  // put result into 565 format
                  "vsli.i16       q2, q1, #5                      \n\t"   // shift up green and insert into blue
                  "vsli.i16       q2, q0, #11                     \n\t"   // shift up red and insert into blue
                  "vst1.16        {d4, d5}, [%[dst]]!             \n\t"   // store result
                  "bgt            1b                              \n\t"   // loop if count > 0
                  : [src] "+r" (src), [dst] "+r" (dst), [count] "+r" (count)
                  : [dstart] "r" (dstart), [scale] "r" (scale)
                  : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                  );
    
    DITHER_565_SCAN(y);
    
    while((count & 7) > 0)
    {
        SkPMColor c = *src++;
        
        int dither = DITHER_VALUE(x);
        int sr = SkGetPackedR32(c);
        int sg = SkGetPackedG32(c);
        int sb = SkGetPackedB32(c);
        sr = SkDITHER_R32To565(sr, dither);
        sg = SkDITHER_G32To565(sg, dither);
        sb = SkDITHER_B32To565(sb, dither);
        
        uint16_t d = *dst;
        *dst++ = SkPackRGB16(SkAlphaBlend(sr, SkGetPackedR16(d), scale),
                             SkAlphaBlend(sg, SkGetPackedG16(d), scale),
                             SkAlphaBlend(sb, SkGetPackedB16(d), scale));
        DITHER_INC_X(x);
        count--;
    }
}

#define S32A_D565_Opaque_PROC       S32A_D565_Opaque_neon
#define S32A_D565_Blend_PROC        S32A_D565_Blend_neon
#define S32_D565_Blend_Dither_PROC  S32_D565_Blend_Dither_neon
#else
#define S32A_D565_Opaque_PROC       NULL
#define S32A_D565_Blend_PROC        NULL
#define S32_D565_Blend_Dither_PROC  NULL
#endif

/* Don't have a special version that assumes each src is opaque, but our S32A
    is still faster than the default, so use it here
 */
#define S32_D565_Opaque_PROC    S32A_D565_Opaque_PROC
#define S32_D565_Blend_PROC     S32A_D565_Blend_PROC

///////////////////////////////////////////////////////////////////////////////

#if defined(__ARM_HAVE_NEON) && defined(SK_CPU_LENDIAN)

static void S32A_Opaque_BlitRow32_neon(SkPMColor* SK_RESTRICT dst,
                                  const SkPMColor* SK_RESTRICT src,
                                  int count, U8CPU alpha) {

    SkASSERT(255 == alpha);
    if (count > 0) {

	/* do the NEON unrolled code */
#define	UNROLL	4
	while (count >= UNROLL) {
	    uint8x8_t src_raw, dst_raw, dst_final;
	    uint8x8_t src_raw_2, dst_raw_2, dst_final_2;
	    uint8x8_t alpha_mask;

	    /* use vtbl, with src_raw as the table */
	    /* expect gcc to hoist alpha_mask setup above loop */
	    alpha_mask = vdup_n_u8(3);
	    alpha_mask = vset_lane_u8(7, alpha_mask, 4);
	    alpha_mask = vset_lane_u8(7, alpha_mask, 5);
	    alpha_mask = vset_lane_u8(7, alpha_mask, 6);
	    alpha_mask = vset_lane_u8(7, alpha_mask, 7);

	    /* get the source */
	    src_raw = vreinterpret_u8_u32(vld1_u32(src));
#if	UNROLL > 2
	    src_raw_2 = vreinterpret_u8_u32(vld1_u32(src+2));
#endif

	    /* get and hold the dst too */
	    dst_raw = vreinterpret_u8_u32(vld1_u32(dst));
#if	UNROLL > 2
	    dst_raw_2 = vreinterpret_u8_u32(vld1_u32(dst+2));
#endif

#if 1
	/* 1st and 2nd bits of the unrolling */
	{
	    uint8x8_t dst_cooked;
	    uint16x8_t dst_wide;
	    uint8x8_t alpha_narrow;
	    uint16x8_t alpha_wide;

	    /* get the alphas spread out properly */
	    alpha_narrow = vtbl1_u8(src_raw, alpha_mask);
	    alpha_narrow = vsub_u8(vdup_n_u8(255), alpha_narrow);
	    alpha_wide = vmovl_u8(alpha_narrow);
	    alpha_wide = vaddq_u16(alpha_wide, vshrq_n_u16(alpha_wide,7));

	    /* get the dest, spread it */
	    dst_raw = vreinterpret_u8_u32(vld1_u32(dst));
	    dst_wide = vmovl_u8(dst_raw);

	    /* alpha mul the dest */
	    dst_wide = vmulq_u16 (dst_wide, alpha_wide);
	    dst_cooked = vshrn_n_u16(dst_wide, 8);

	    /* sum -- ignoring any byte lane overflows */
	    dst_final = vadd_u8(src_raw, dst_cooked);
	}
#endif

#if	UNROLL > 2
	/* the 3rd and 4th bits of our unrolling */
	{
	    uint8x8_t dst_cooked;
	    uint16x8_t dst_wide;
	    uint8x8_t alpha_narrow;
	    uint16x8_t alpha_wide;

	    alpha_narrow = vtbl1_u8(src_raw_2, alpha_mask);
	    alpha_narrow = vsub_u8(vdup_n_u8(255), alpha_narrow);
	    alpha_wide = vmovl_u8(alpha_narrow);
	    alpha_wide = vaddq_u16(alpha_wide, vshrq_n_u16(alpha_wide,7));

	    /* get the dest, spread it */
	    dst_wide = vmovl_u8(dst_raw_2);

	    /* alpha mul the dest */
	    dst_wide = vmulq_u16 (dst_wide, alpha_wide);
	    dst_cooked = vshrn_n_u16(dst_wide, 8);

	    /* sum -- ignoring any byte lane overflows */
	    dst_final_2 = vadd_u8(src_raw_2, dst_cooked);
	}
#endif

	    vst1_u32(dst, vreinterpret_u32_u8(dst_final));
#if	UNROLL > 2
	    vst1_u32(dst+2, vreinterpret_u32_u8(dst_final_2));
#endif

	    src += UNROLL;
	    dst += UNROLL;
	    count -= UNROLL;
	}
#undef	UNROLL

	/* do any residual iterations */
        while (--count >= 0) {
#ifdef TEST_SRC_ALPHA
            SkPMColor sc = *src;
            if (sc) {
                unsigned srcA = SkGetPackedA32(sc);
                SkPMColor result = sc;
                if (srcA != 255) {
                    result = SkPMSrcOver(sc, *dst);
                }
                *dst = result;
            }
#else
            *dst = SkPMSrcOver(*src, *dst);
#endif
            src += 1;
            dst += 1;
        }
    }
}

#define	S32A_Opaque_BlitRow32_PROC	S32A_Opaque_BlitRow32_neon
#else
#define	S32A_Opaque_BlitRow32_PROC	NULL
#endif

/* Neon version of S32_Blend_BlitRow32()
 * portable version is in core/SkBlitRow_D32.cpp
 */
#if defined(__ARM_HAVE_NEON) && defined(SK_CPU_LENDIAN)
static void S32_Blend_BlitRow32_neon(SkPMColor* SK_RESTRICT dst,
                                const SkPMColor* SK_RESTRICT src,
                                int count, U8CPU alpha) {
    SkASSERT(alpha <= 255);
    if (count > 0) {
        uint16_t src_scale = SkAlpha255To256(alpha);
        uint16_t dst_scale = 256 - src_scale;

	/* run them N at a time through the NEON unit */
	/* note that each 1 is 4 bytes, each treated exactly the same,
	 * so we can work under that guise. We *do* know that the src&dst
	 * will be 32-bit aligned quantities, so we can specify that on
	 * the load/store ops and do a neon 'reinterpret' to get us to
	 * byte-sized (pun intended) pieces that we widen/multiply/shift
	 * we're limited at 128 bits in the wide ops, which is 8x16bits
	 * or a pair of 32 bit src/dsts.
	 */
	/* we *could* manually unroll this loop so that we load 128 bits
	 * (as a pair of 64s) from each of src and dst, processing them
	 * in pieces. This might give us a little better management of
	 * the memory latency, but my initial attempts here did not
	 * produce an instruction stream that looked all that nice.
	 */
#define	UNROLL	2
	while (count >= UNROLL) {
	    uint8x8_t  src_raw, dst_raw, dst_final;
	    uint16x8_t  src_wide, dst_wide;

	    /* get 64 bits of src, widen it, multiply by src_scale */
	    src_raw = vreinterpret_u8_u32(vld1_u32(src));
	    src_wide = vmovl_u8(src_raw);
	    /* gcc hoists vdupq_n_u16(), better code than vmulq_n_u16() */
	    src_wide = vmulq_u16 (src_wide, vdupq_n_u16(src_scale));

	    /* ditto with dst */
	    dst_raw = vreinterpret_u8_u32(vld1_u32(dst));
	    dst_wide = vmovl_u8(dst_raw);
	    dst_wide = vmulq_u16 (dst_wide, vdupq_n_u16(dst_scale));

	    /* sum (knowing it won't overflow 16 bits) and take high bits */
	    dst_wide = vaddq_u16(dst_wide, src_wide);
	    dst_final = vshrn_n_u16(dst_wide, 8);

	    vst1_u32(dst, vreinterpret_u32_u8(dst_final));

	    src += UNROLL;
	    dst += UNROLL;
	    count -= UNROLL;
	}
	/* RBE: well, i don't like how gcc manages src/dst across the above
	 * loop it's constantly calculating src+bias, dst+bias and it only
	 * adjusts the real ones when we leave the loop. Not sure why
	 * it's "hoisting down" (hoisting implies above in my lexicon ;))
	 * the adjustments to src/dst/count, but it does...
	 * (might be SSA-style internal logic...
	 */

#if	UNROLL == 2
	if (count == 1) {
            *dst = SkAlphaMulQ(*src, src_scale) + SkAlphaMulQ(*dst, dst_scale);
	}
#else
	if (count > 0) {
            do {
                *dst = SkAlphaMulQ(*src, src_scale) + SkAlphaMulQ(*dst, dst_scale);
                src += 1;
                dst += 1;
            } while (--count > 0);
	}
#endif

#undef	UNROLL
    }
}

#define	S32_Blend_BlitRow32_PROC	S32_Blend_BlitRow32_neon
#else
#define	S32_Blend_BlitRow32_PROC	NULL
#endif

///////////////////////////////////////////////////////////////////////////////

#if defined(__ARM_HAVE_NEON) && defined(SK_CPU_LENDIAN)
/* RBE: working on this 2009/10/8 */
static void S32A_D565_Opaque_Dither_neon(uint16_t* SK_RESTRICT dst,
                                      const SkPMColor* SK_RESTRICT src,
                                      int count, U8CPU alpha, int x, int y) {
    SkASSERT(255 == alpha);
    
    if (count > 0) {
        DITHER_565_SCAN(y);
        do {
            SkPMColor c = *src++;
            SkPMColorAssert(c);
	/* RBE: make sure we don't generate wrong output if c==0 */
            if (c) {

	/* let's do a vld4 to get 64 bits (8 bytes) of each Argb */
	/* so we'll have 8 a's, 8 r's, etc */
		/* little endian: ABGR is the ordering (R at lsb) */
                unsigned a = SkGetPackedA32(c);
                
	// RBE: could load a table and do vtbl for these things
	// DITHER_VALUE() masks x to 3 bits [0..7] before lookup, so can
	// so 8x unrolling gets us perfectly aligned.
	// and we could even avoid the vtbl at that point
	/* d is 0..7 according to skia/core/SkDither.h asserts */
                int d = SkAlphaMul(DITHER_VALUE(x), SkAlpha255To256(a));
                
                unsigned sr = SkGetPackedR32(c);
                unsigned sg = SkGetPackedG32(c);
                unsigned sb = SkGetPackedB32(c);

	/* R and B handled identically; G is a little different */

		/* sr - (sr>>5) means that +d can NOT overflow */
		/* do (sr-(sr>>5)), followed by adding d -- stay in 8 bits */
		/* sr = sr+d - (sr>>5) */
                sr = SkDITHER_R32_FOR_565(sr, d);
	/* calculate sr+(sr>>5) here, then add d */

		/* sg = sg + (d>>1) - (sg>>6) */
                sg = SkDITHER_G32_FOR_565(sg, d);
		/* sg>>6 could be '3' and d>>1 is <= 3, so we're ok */
	/* calculate sg-(sg>>6), then add "d>>1" */
		

		/* sb = sb+d - (sb>>5) */
                sb = SkDITHER_B32_FOR_565(sb, d);
	/* calculate sb+(sb>>5) here, then add d */
                

	/* been dealing in 8x8 through here; gonna have to go to 8x16 */

	/* need to pick up 8 dst's -- at 16 bits each, 256 bits */
	/* extract dst into 8x16's */
	/* blend */
	/* shift */
	/* reassemble */

                uint32_t src_expanded = (sg << 24) | (sr << 13) | (sb << 2);
                uint32_t dst_expanded = SkExpand_rgb_16(*dst);

	// would be shifted by 8, but the >>3 makes it be just 5 
                dst_expanded = dst_expanded * (SkAlpha255To256(255 - a) >> 3);
                // now src and dst expanded are in g:11 r:10 x:1 b:10
                *dst = SkCompact_rgb_16((src_expanded + dst_expanded) >> 5);
            }
            dst += 1;
        /* RBE: a NOP with wide enough unrolling; wide_enough == 8 */
            DITHER_INC_X(x);
        } while (--count != 0);
    }
}

#define	S32A_D565_Opaque_Dither_PROC S32A_D565_Opaque_Dither_neon
#else
#define	S32A_D565_Opaque_Dither_PROC NULL
#endif

///////////////////////////////////////////////////////////////////////////////

const SkBlitRow::Proc SkBlitRow::gPlatform_565_Procs[] = {
    // no dither
    S32_D565_Opaque_PROC,
    S32_D565_Blend_PROC,
    S32A_D565_Opaque_PROC,
#if 0
    // when the src-pixel is 0 (transparent), we are still affecting the dst
    // so we're skipping this optimization for now
    S32A_D565_Blend_PROC,
#else
    NULL,
#endif
    
    // dither
    NULL,   // S32_D565_Opaque_Dither,
    S32_D565_Blend_Dither_PROC,
    S32A_D565_Opaque_Dither_PROC,
    NULL,   // S32A_D565_Blend_Dither
};

const SkBlitRow::Proc SkBlitRow::gPlatform_4444_Procs[] = {
    // no dither
    NULL,   // S32_D4444_Opaque,
    NULL,   // S32_D4444_Blend,
    NULL,   // S32A_D4444_Opaque,
    NULL,   // S32A_D4444_Blend,
    
    // dither
    NULL,   // S32_D4444_Opaque_Dither,
    NULL,   // S32_D4444_Blend_Dither,
    NULL,   // S32A_D4444_Opaque_Dither,
    NULL,   // S32A_D4444_Blend_Dither
};

const SkBlitRow::Proc32 SkBlitRow::gPlatform_Procs32[] = {
    NULL,   // S32_Opaque,
    S32_Blend_BlitRow32_PROC,		// S32_Blend,
    S32A_Opaque_BlitRow32_PROC,		// S32A_Opaque,
    NULL,   // S32A_Blend,
};
