/* ft -- A basic file transfer utility.
 * by Mibi88
 *
 * This software is licensed under the BSD-3-Clause license:
 *
 * Copyright 2025 Mibi88
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* SHA256, as described in FIPS PUB 180-4. I'm following the specs.' names very
 * closely. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <hash.h>

/* Macro used to load the message into the message schedule. */
#define LOOP4(l)    l;l;l;l

/* Macros to add the working variables to the hash. */
#define LOOP8(l)    l(0);l(1);l(2);l(3);l(4);l(5);l(6);l(7)
#define _ADD_HASH(i) h[i] = (h[i]+tmp[i])&0xFFFFFFFF
#define ADD_HASH() LOOP8(_ADD_HASH)

/* Functions described in section 2.2.2. */
#define ROTR(x, n)   ((((x)>>(n))&0xFFFFFFFF)|(((x)<<(32-n))&0xFFFFFFFF))
#define SHR(x, n)    (((x)>>(n))&0xFFFFFFFF)

/* Functions described in section 4.1.2. */
#define CH(x, y, z)  (((x)&(y))^((~(x)&0xFFFFFFFF)&(z)))
#define MAJ(x, y, z) (((x)&(y))^((x)&(z))^((y)&(z)))

#define SUM0(x)      (ROTR(x, 2)^ROTR(x, 13)^ROTR(x, 22))
#define SUM1(x)      (ROTR(x, 6)^ROTR(x, 11)^ROTR(x, 25))
#define SIGMA0(x)    (ROTR(x, 7)^ROTR(x, 18)^SHR(x, 3))
#define SIGMA1(x)    (ROTR(x, 17)^ROTR(x, 19)^SHR(x, 10))

/* Working variables described in section 6.2. */
#define A tmp[0]
#define B tmp[1]
#define C tmp[2]
#define D tmp[3]
#define E tmp[4]
#define F tmp[5]
#define G tmp[6]
#define H tmp[7]

#define SHA256(h, get_msg, size) \
    { \
        /* SHA-256 constants listed in section 4.2.2.
         * NOTE: this array is called `K' in the spec., but I named it
         *       differently as I have another variable called `k'. */ \
        static const word_t consts[64] = { \
             0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, \
             0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5, \
             0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, \
             0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174, \
             0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, \
             0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA, \
             0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, \
             0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967, \
             0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, \
             0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85, \
             0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, \
             0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070, \
             0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, \
             0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3, \
             0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, \
             0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2 \
        }; \
 \
        /* Initial hash values */ \
        static const word_t init_hash[8] = { \
             0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A, \
             0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19 \
        }; \
 \
        /* Message schedule */ \
        static word_t w[64]; \
 \
        /* Array containing the 8 working variables (accessed with macros A, B,
         * C, D, E, F, G, H). */ \
        static word_t tmp[8]; \
 \
        size_t i, t; \
        size_t k = 64-(size+9)%64; \
 \
        memcpy(h, init_hash, 8*sizeof(word_t)); \
 \
        for(i=0;i<size+1+k+8;){ \
            register unsigned char v; \
 \
            /* Prepare the message schedule */ \
            for(t=0;t<16;t++){ \
                w[t] = 0; \
                LOOP4({ \
                    if(i < size){ \
                        v = get_msg; \
                    }else if(i == size){ \
                        v = 1<<7; \
                    }else if(i < size+1+k){ \
                        v = 0; \
                    }else{ \
                        v = (size*8)>>(8-(i-(size+k)))*8; \
                    } \
                    w[t] <<= 8; \
                    w[t] |= v; \
 \
                    i++; \
                }); \
            } \
 \
            for(t=16;t<64;t++){ \
                w[t] = (SIGMA1(w[t-2])+w[t-7]+SIGMA0(w[t-15]) \
                        +w[t-16])&0xFFFFFFFF; \
            } \
 \
            /* Initialize the eight working variables */ \
            memcpy(tmp, h, 8*sizeof(word_t)); \
 \
            /* Calculate the intermediate hash value */ \
            for(t=0;t<64;t++){ \
                register word_t t1, t2; \
                t1 = H+SUM1(E)+CH(E, F, G)+consts[t]+w[t]; \
                t2 = SUM0(A)+MAJ(A, B, C); \
                H = G; \
                G = F; \
                F = E; \
                E = (D+t1)&0xFFFFFFFF; \
                D = C; \
                C = B; \
                B = A; \
                A = (t1+t2)&0xFFFFFFFF; \
            } \
 \
            ADD_HASH(); \
        } \
    }

void sha256(word_t h[8], unsigned char *msg, size_t size) {
    SHA256(h, msg[i], size);
}

void sha256_fnc(word_t h[8], unsigned char msg_get_next(void *data),
                size_t size, void *data) {
    SHA256(h, msg_get_next(data), size);
}
