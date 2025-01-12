/* gost.c - an implementation of GOST Hash Function
 * based on the Russian Standard GOST R 34.11-94.
 * See also RFC 4357.
 *
 * Copyright: 2009-2012 Aleksey Kravchenko <rhash.admin@gmail.com>
 *
 * Permission is hereby granted,  free of charge,  to any person  obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction,  including without limitation
 * the rights to  use, copy, modify,  merge, publish, distribute, sublicense,
 * and/or sell copies  of  the Software,  and to permit  persons  to whom the
 * Software is furnished to do so.
 *
 * This program  is  distributed  in  the  hope  that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  Use this program  at  your own risk!
 * https://github.com/rhash/RHash/blob/master/librhash/gost.c
 */

#include <cstring>
#include "../whirlpool/byte_order.h"
#include "gost.h"

extern unsigned rhash_gost_sbox[4][256];
extern unsigned rhash_gost_sbox_cryptpro[4][256];

/**
 * Initialize algorithm context before calculaing hash
 * with test parameters set.
 *
 * @param ctx context to initialize
 */
void rhash_gost_init(gost_ctx *ctx)
{
    memset(ctx, 0, sizeof(gost_ctx));
}

/**
 * Initialize GOST algorithm context with CryptoPro parameter set.
 *
 * @param ctx context to initialize
 */
void rhash_gost_cryptopro_init(gost_ctx *ctx)
{
    rhash_gost_init(ctx);
    ctx->cryptpro = 1;
}

#if defined(__GNUC__) && defined(CPU_IA32) && !defined(__clang__) && !defined(RHASH_NO_ASM)
# define USE_GCC_ASM_IA32
#elif defined(__GNUC__) && defined(CPU_X64) && !defined(RHASH_NO_ASM)
# define USE_GCC_ASM_X64
#endif

/*
 *  A macro that performs a full encryption round of GOST 28147-89.
 *  Temporary variables tmp assumed and variables r and l for left and right
 *  blocks.
 */
# define GOST_ENCRYPT_ROUND(key1, key2, sbox) \
	tmp = (key1) + r; \
	l ^= (sbox)[tmp & 0xff] ^ ((sbox) + 256)[(tmp >> 8) & 0xff] ^ \
		((sbox) + 512)[(tmp >> 16) & 0xff] ^ ((sbox) + 768)[tmp >> 24]; \
	tmp = (key2) + l; \
	r ^= (sbox)[tmp & 0xff] ^ ((sbox) + 256)[(tmp >> 8) & 0xff] ^ \
		((sbox) + 512)[(tmp >> 16) & 0xff] ^ ((sbox) + 768)[tmp >> 24];

# define GOST_ENCRYPT_ROUND_L(key1, key2, sbox) \
	tmp = (key1) + r; \
	l ^= (sbox)[tmp & 0xff] ^ ((sbox) + 256)[(tmp >> 8) & 0xff] ^ \
		((sbox) + 512)[(tmp >> 16) & 0xff] ^ ((sbox) + 768)[tmp >> 24]; \

# define GOST_ENCRYPT_ROUND_R(key1, key2, sbox) \
	tmp = (key2) + l; \
	r ^= (sbox)[tmp & 0xff] ^ ((sbox) + 256)[(tmp >> 8) & 0xff] ^ \
		((sbox) + 512)[(tmp >> 16) & 0xff] ^ ((sbox) + 768)[tmp >> 24];

/* encrypt a block with the given key */
/* ph4r05: round reduced version from the GOST_ENCRYPT macro */
void gost_encrypt_fnc(unsigned int * result, unsigned int i, const unsigned int * key, const unsigned int * hash,
                      const unsigned int * sbox, unsigned nr)
{
    unsigned l, r, tmp, jj;
    r = hash[i], l = hash[i + 1];
    for(jj = 0; jj < 12 && 2*jj < nr; ++jj){
        GOST_ENCRYPT_ROUND_L(key[(2 * jj + 0) % 8], key[(2 * jj + 1) % 8], sbox)
        if (2*jj + 1 < nr) {
            GOST_ENCRYPT_ROUND_R(key[(2 * jj + 0) % 8], key[(2 * jj + 1) % 8], sbox)
        }
    }
    for(jj = 12; jj < 16 && 2*jj < nr; ++jj){
        GOST_ENCRYPT_ROUND_L(key[2 * (16 - jj) - 1], key[2 * (16 - jj) - 2], sbox)
        if (2*jj + 1 < nr) {
            GOST_ENCRYPT_ROUND_R(key[2 * (16 - jj) - 1], key[2 * (16 - jj) - 2], sbox)
        }
    }

    result[i] = l, result[i + 1] = r;
}

/* encrypt a block with the given key */
/* Original round function, kept for reference */
# define GOST_ENCRYPT(result, i, key, hash, sbox) \
	r = hash[i], l = hash[i + 1]; \
	GOST_ENCRYPT_ROUND(key[0], key[1], sbox) \
	GOST_ENCRYPT_ROUND(key[2], key[3], sbox) \
    GOST_ENCRYPT_ROUND(key[4], key[5], sbox) \
    GOST_ENCRYPT_ROUND(key[6], key[7], sbox) \
    GOST_ENCRYPT_ROUND(key[0], key[1], sbox) \
    GOST_ENCRYPT_ROUND(key[2], key[3], sbox) \
    GOST_ENCRYPT_ROUND(key[4], key[5], sbox) \
    GOST_ENCRYPT_ROUND(key[6], key[7], sbox) \
	GOST_ENCRYPT_ROUND(key[0], key[1], sbox) \
	GOST_ENCRYPT_ROUND(key[2], key[3], sbox) \
	GOST_ENCRYPT_ROUND(key[4], key[5], sbox) \
	GOST_ENCRYPT_ROUND(key[6], key[7], sbox) \
	GOST_ENCRYPT_ROUND(key[7], key[6], sbox) \
	GOST_ENCRYPT_ROUND(key[5], key[4], sbox) \
	GOST_ENCRYPT_ROUND(key[3], key[2], sbox) \
	GOST_ENCRYPT_ROUND(key[1], key[0], sbox) \
	result[i] = l, result[i + 1] = r;


/**
 * The core transformation. Process a 512-bit block.
 *
 * @param hash intermediate message hash
 * @param block the message block to process
 */
static void rhash_gost_block_compress(gost_ctx *ctx, const unsigned* block, unsigned nr)
{
    unsigned i;
    unsigned key[8], u[8], v[8], w[8], s[8];
    unsigned *sbox = (ctx->cryptpro ? (unsigned*)rhash_gost_sbox_cryptpro : (unsigned*)rhash_gost_sbox);

    /* u := hash, v := <256-bit message block> */
    memcpy(u, ctx->hash, sizeof(u));
    memcpy(v, block, sizeof(v));

    /* w := u xor v */
    w[0] = u[0] ^ v[0], w[1] = u[1] ^ v[1];
    w[2] = u[2] ^ v[2], w[3] = u[3] ^ v[3];
    w[4] = u[4] ^ v[4], w[5] = u[5] ^ v[5];
    w[6] = u[6] ^ v[6], w[7] = u[7] ^ v[7];

    /* calculate keys, encrypt hash and store result to the s[] array */
    for (i = 0;; i += 2) {
        /* key generation: key_i := P(w) */
        key[0] = (w[0] & 0x000000ff) | ((w[2] & 0x000000ff) << 8) | ((w[4] & 0x000000ff) << 16) | ((w[6] & 0x000000ff) << 24);
        key[1] = ((w[0] & 0x0000ff00) >> 8) | (w[2] & 0x0000ff00) | ((w[4] & 0x0000ff00) << 8)  | ((w[6] & 0x0000ff00) << 16);
        key[2] = ((w[0] & 0x00ff0000) >> 16) | ((w[2] & 0x00ff0000) >> 8) | (w[4] & 0x00ff0000) | ((w[6] & 0x00ff0000) << 8);
        key[3] = ((w[0] & 0xff000000) >> 24) | ((w[2] & 0xff000000) >> 16) | ((w[4] & 0xff000000) >> 8) | (w[6] & 0xff000000);
        key[4] = (w[1] & 0x000000ff) | ((w[3] & 0x000000ff) << 8) | ((w[5] & 0x000000ff) << 16) | ((w[7] & 0x000000ff) << 24);
        key[5] = ((w[1] & 0x0000ff00) >> 8) | (w[3] & 0x0000ff00) | ((w[5] & 0x0000ff00) << 8)  | ((w[7] & 0x0000ff00) << 16);
        key[6] = ((w[1] & 0x00ff0000) >> 16) | ((w[3] & 0x00ff0000) >> 8) | (w[5] & 0x00ff0000) | ((w[7] & 0x00ff0000) << 8);
        key[7] = ((w[1] & 0xff000000) >> 24) | ((w[3] & 0xff000000) >> 16) | ((w[5] & 0xff000000) >> 8) | (w[7] & 0xff000000);

        /* encryption: s_i := E_{key_i} (h_i) */
        {
//             unsigned l, r, tmp;
//             GOST_ENCRYPT(s, i, key, ctx->hash, sbox);
            gost_encrypt_fnc(s, i, key, ctx->hash, sbox, nr);
        }

        if (i == 0) {
            /* w:= A(u) ^ A^2(v) */
            w[0] = u[2] ^ v[4], w[1] = u[3] ^ v[5];
            w[2] = u[4] ^ v[6], w[3] = u[5] ^ v[7];
            w[4] = u[6] ^ (v[0] ^= v[2]);
            w[5] = u[7] ^ (v[1] ^= v[3]);
            w[6] = (u[0] ^= u[2]) ^ (v[2] ^= v[4]);
            w[7] = (u[1] ^= u[3]) ^ (v[3] ^= v[5]);
        } else if ((i & 2) != 0) {
            if (i == 6) break;

            /* w := A^2(u) xor A^4(v) xor C_3; u := A(u) xor C_3 */
            /* C_3=0xff00ffff000000ffff0000ff00ffff0000ff00ff00ff00ffff00ff00ff00ff00 */
            u[2] ^= u[4] ^ 0x000000ff;
            u[3] ^= u[5] ^ 0xff00ffff;
            u[4] ^= 0xff00ff00;
            u[5] ^= 0xff00ff00;
            u[6] ^= 0x00ff00ff;
            u[7] ^= 0x00ff00ff;
            u[0] ^= 0x00ffff00;
            u[1] ^= 0xff0000ff;

            w[0] = u[4] ^ v[0];
            w[2] = u[6] ^ v[2];
            w[4] = u[0] ^ (v[4] ^= v[6]);
            w[6] = u[2] ^ (v[6] ^= v[0]);
            w[1] = u[5] ^ v[1];
            w[3] = u[7] ^ v[3];
            w[5] = u[1] ^ (v[5] ^= v[7]);
            w[7] = u[3] ^ (v[7] ^= v[1]);
        } else {
            /* i==4 here */
            /* w:= A( A^2(u) xor C_3 ) xor A^6(v) */
            w[0] = u[6] ^ v[4], w[1] = u[7] ^ v[5];
            w[2] = u[0] ^ v[6], w[3] = u[1] ^ v[7];
            w[4] = u[2] ^ (v[0] ^= v[2]);
            w[5] = u[3] ^ (v[1] ^= v[3]);
            w[6] = (u[4] ^= u[6]) ^ (v[2] ^= v[4]);
            w[7] = (u[5] ^= u[7]) ^ (v[3] ^= v[5]);
        }
    }

    /* step hash function: x(block, hash) := psi^61(hash xor psi(block xor psi^12(S))) */

    /* 12 rounds of the LFSR and xor in <message block> */
    u[0] = block[0] ^ s[6];
    u[1] = block[1] ^ s[7];
    u[2] = block[2] ^ (s[0] << 16) ^ (s[0] >> 16) ^ (s[0] & 0xffff) ^ (s[1] & 0xffff) ^ (s[1] >> 16) ^ (s[2] << 16) ^ s[6] ^ (s[6] << 16) ^ (s[7] & 0xffff0000) ^ (s[7] >> 16);
    u[3] = block[3] ^ (s[0] & 0xffff) ^ (s[0] << 16) ^ (s[1] & 0xffff) ^ (s[1] << 16) ^ (s[1] >> 16) ^
           (s[2] << 16) ^ (s[2] >> 16) ^ (s[3] << 16) ^ s[6] ^ (s[6] << 16) ^ (s[6] >> 16) ^ (s[7] & 0xffff) ^ (s[7] << 16) ^ (s[7] >> 16);
    u[4] = block[4] ^ (s[0] & 0xffff0000) ^ (s[0] << 16) ^ (s[0] >> 16) ^
           (s[1] & 0xffff0000) ^ (s[1] >> 16) ^ (s[2] << 16) ^ (s[2] >> 16) ^ (s[3] << 16) ^ (s[3] >> 16) ^ (s[4] << 16) ^ (s[6] << 16) ^ (s[6] >> 16) ^ (s[7] & 0xffff) ^ (s[7] << 16) ^ (s[7] >> 16);
    u[5] = block[5] ^ (s[0] << 16) ^ (s[0] >> 16) ^ (s[0] & 0xffff0000) ^
           (s[1] & 0xffff) ^ s[2] ^ (s[2] >> 16) ^ (s[3] << 16) ^ (s[3] >> 16) ^ (s[4] << 16) ^ (s[4] >> 16) ^ (s[5] << 16) ^ (s[6] << 16) ^ (s[6] >> 16) ^ (s[7] & 0xffff0000) ^ (s[7] << 16) ^ (s[7] >> 16);
    u[6] = block[6] ^ s[0] ^ (s[1] >> 16) ^ (s[2] << 16) ^ s[3] ^ (s[3] >> 16)
           ^ (s[4] << 16) ^ (s[4] >> 16) ^ (s[5] << 16) ^ (s[5] >> 16) ^ s[6] ^ (s[6] << 16) ^ (s[6] >> 16) ^ (s[7] << 16);
    u[7] = block[7] ^ (s[0] & 0xffff0000) ^ (s[0] << 16) ^ (s[1] & 0xffff) ^
           (s[1] << 16) ^ (s[2] >> 16) ^ (s[3] << 16) ^ s[4] ^ (s[4] >> 16) ^ (s[5] << 16) ^ (s[5] >> 16) ^ (s[6] >> 16) ^ (s[7] & 0xffff) ^ (s[7] << 16) ^ (s[7] >> 16);

    /* 1 round of the LFSR (a mixing transformation) and xor with <hash> */
    v[0] = ctx->hash[0] ^ (u[1] << 16) ^ (u[0] >> 16);
    v[1] = ctx->hash[1] ^ (u[2] << 16) ^ (u[1] >> 16);
    v[2] = ctx->hash[2] ^ (u[3] << 16) ^ (u[2] >> 16);
    v[3] = ctx->hash[3] ^ (u[4] << 16) ^ (u[3] >> 16);
    v[4] = ctx->hash[4] ^ (u[5] << 16) ^ (u[4] >> 16);
    v[5] = ctx->hash[5] ^ (u[6] << 16) ^ (u[5] >> 16);
    v[6] = ctx->hash[6] ^ (u[7] << 16) ^ (u[6] >> 16);
    v[7] = ctx->hash[7] ^ (u[0] & 0xffff0000) ^ (u[0] << 16) ^ (u[1] & 0xffff0000) ^ (u[1] << 16) ^ (u[6] << 16) ^ (u[7] & 0xffff0000) ^ (u[7] >> 16);

    /* 61 rounds of LFSR, mixing up hash */
    ctx->hash[0] = (v[0] & 0xffff0000) ^ (v[0] << 16) ^ (v[0] >> 16) ^
                   (v[1] >> 16) ^ (v[1] & 0xffff0000) ^ (v[2] << 16) ^
                   (v[3] >> 16) ^ (v[4] << 16) ^ (v[5] >> 16) ^ v[5] ^
                   (v[6] >> 16) ^ (v[7] << 16) ^ (v[7] >> 16) ^ (v[7] & 0xffff);
    ctx->hash[1] = (v[0] << 16) ^ (v[0] >> 16) ^ (v[0] & 0xffff0000) ^
                   (v[1] & 0xffff) ^ v[2] ^ (v[2] >> 16) ^ (v[3] << 16) ^
                   (v[4] >> 16) ^ (v[5] << 16) ^ (v[6] << 16) ^ v[6] ^
                   (v[7] & 0xffff0000) ^ (v[7] >> 16);
    ctx->hash[2] = (v[0] & 0xffff) ^ (v[0] << 16) ^ (v[1] << 16) ^
                   (v[1] >> 16) ^ (v[1] & 0xffff0000) ^ (v[2] << 16) ^ (v[3] >> 16) ^
                   v[3] ^ (v[4] << 16) ^ (v[5] >> 16) ^ v[6] ^ (v[6] >> 16) ^
                   (v[7] & 0xffff) ^ (v[7] << 16) ^ (v[7] >> 16);
    ctx->hash[3] = (v[0] << 16) ^ (v[0] >> 16) ^ (v[0] & 0xffff0000) ^
                   (v[1] & 0xffff0000) ^ (v[1] >> 16) ^ (v[2] << 16) ^
                   (v[2] >> 16) ^ v[2] ^ (v[3] << 16) ^ (v[4] >> 16) ^ v[4] ^
                   (v[5] << 16) ^ (v[6] << 16) ^ (v[7] & 0xffff) ^ (v[7] >> 16);
    ctx->hash[4] = (v[0] >> 16) ^ (v[1] << 16) ^ v[1] ^ (v[2] >> 16) ^ v[2] ^
                   (v[3] << 16) ^ (v[3] >> 16) ^ v[3] ^ (v[4] << 16) ^
                   (v[5] >> 16) ^ v[5] ^ (v[6] << 16) ^ (v[6] >> 16) ^ (v[7] << 16);
    ctx->hash[5] = (v[0] << 16) ^ (v[0] & 0xffff0000) ^ (v[1] << 16) ^
                   (v[1] >> 16) ^ (v[1] & 0xffff0000) ^ (v[2] << 16) ^ v[2] ^
                   (v[3] >> 16) ^ v[3] ^ (v[4] << 16) ^ (v[4] >> 16) ^ v[4] ^
                   (v[5] << 16) ^ (v[6] << 16) ^ (v[6] >> 16) ^ v[6] ^
                   (v[7] << 16) ^ (v[7] >> 16) ^ (v[7] & 0xffff0000);
    ctx->hash[6] = v[0] ^ v[2] ^ (v[2] >> 16) ^ v[3] ^ (v[3] << 16) ^ v[4] ^
                   (v[4] >> 16) ^ (v[5] << 16) ^ (v[5] >> 16) ^ v[5] ^
                   (v[6] << 16) ^ (v[6] >> 16) ^ v[6] ^ (v[7] << 16) ^ v[7];
    ctx->hash[7] = v[0] ^ (v[0] >> 16) ^ (v[1] << 16) ^ (v[1] >> 16) ^
                   (v[2] << 16) ^ (v[3] >> 16) ^ v[3] ^ (v[4] << 16) ^ v[4] ^
                   (v[5] >> 16) ^ v[5] ^ (v[6] << 16) ^ (v[6] >> 16) ^ (v[7] << 16) ^ v[7];
}

/**
 * This function calculates hash value by 256-bit blocks.
 * It updates 256-bit check sum as follows:
 *    *(uint256_t)(ctx->sum) += *(uint256_t*)block;
 * and then updates intermediate hash value ctx->hash
 * by calling rhash_gost_block_compress().
 *
 * @param ctx algorithm context
 * @param block the 256-bit message block to process
 */
static void rhash_gost_compute_sum_and_hash(gost_ctx * ctx, const unsigned* block, unsigned nr)
{
#if IS_LITTLE_ENDIAN
    # define block_le block
# define LOAD_BLOCK_LE(i)
#else
    unsigned block_le[8]; /* tmp buffer for little endian number */
# define LOAD_BLOCK_LE(i) (block_le[i] = le2me_32(block[i]))
#endif

    /* This optimization doesn't improve speed much,
    * and saves too little memory, but it was fun to write! =)  */
#ifdef USE_GCC_ASM_IA32
    __asm __volatile(
		"addl %0, (%1)\n\t"
		"movl 4(%2), %0\n\t"
		"adcl %0, 4(%1)\n\t"
		"movl 8(%2), %0\n\t"
		"adcl %0, 8(%1)\n\t"
		"movl 12(%2), %0\n\t"
		"adcl %0, 12(%1)\n\t"
		"movl 16(%2), %0\n\t"
		"adcl %0, 16(%1)\n\t"
		"movl 20(%2), %0\n\t"
		"adcl %0, 20(%1)\n\t"
		"movl 24(%2), %0\n\t"
		"adcl %0, 24(%1)\n\t"
		"movl 28(%2), %0\n\t"
		"adcl %0, 28(%1)\n\t"
		: : "r" (block[0]), "r" (ctx->sum), "r" (block)
		: "0", "memory", "cc" );
#elif defined(USE_GCC_ASM_X64)
    const uint64_t* block64 = (const uint64_t*)block;
	uint64_t* sum64 = (uint64_t*)ctx->sum;
	__asm __volatile(
		"addq %4, %0\n\t"
		"adcq %5, %1\n\t"
		"adcq %6, %2\n\t"
		"adcq %7, %3\n\t"
		: "+m" (sum64[0]), "+m" (sum64[1]), "+m" (sum64[2]), "+m" (sum64[3])
		: "r" (block64[0]), "r" (block64[1]), "r" (block64[2]), "r" (block64[3])
		: "cc" );
#else /* USE_GCC_ASM_IA32 */

    unsigned i, carry = 0;

    /* compute the 256-bit sum */
    for (i = 0; i < 8; i++) {
        LOAD_BLOCK_LE(i);
        ctx->sum[i] += block_le[i] + carry;
        carry = (ctx->sum[i] < block_le[i] ? 1 :
                 ctx->sum[i] == block_le[i] ? carry : 0);
    }
#endif /* USE_GCC_ASM_IA32 */

    /* update message hash */
    rhash_gost_block_compress(ctx, block_le, nr);
}

/**
 * Calculate message hash.
 * Can be called repeatedly with chunks of the message to be hashed.
 *
 * @param ctx the algorithm context containing current hashing state
 * @param msg message chunk
 * @param size length of the message chunk
 */
void rhash_gost_update(gost_ctx *ctx, const unsigned char* msg, size_t size, unsigned nr)
{
    unsigned index = (unsigned)ctx->length & 31;
    ctx->length += size;

    /* fill partial block */
    if (index) {
        unsigned left = gost_block_size - index;
        memcpy(ctx->message + index, msg, (size < left ? size : left));
        if (size < left) return;

        /* process partial block */
        rhash_gost_compute_sum_and_hash(ctx, (unsigned*)ctx->message, nr);
        msg += left;
        size -= left;
    }
    while (size >= gost_block_size) {
        unsigned* aligned_message_block;
#if (defined(__GNUC__) && defined(CPU_X64))
        if (IS_ALIGNED_64(msg)) {
#else
        if (IS_ALIGNED_32(msg)) {
#endif
            /* the most common case is processing of an already aligned message
            on little-endian CPU without copying it */
            aligned_message_block = (unsigned*)msg;
        } else {
            memcpy(ctx->message, msg, gost_block_size);
            aligned_message_block = (unsigned*)ctx->message;
        }

        rhash_gost_compute_sum_and_hash(ctx, aligned_message_block, nr);
        msg += gost_block_size;
        size -= gost_block_size;
    }
    if (size) {
        /* save leftovers */
        memcpy(ctx->message, msg, size);
    }
}

/**
 * Finish hashing and store message digest into given array.
 *
 * @param ctx the algorithm context containing current hashing state
 * @param result calculated hash in binary form
 */
void rhash_gost_final(gost_ctx *ctx, unsigned char result[32], unsigned nr)
{
    unsigned  index = (unsigned)ctx->length & 31;
    unsigned* msg32 = (unsigned*)ctx->message;

    /* pad the last block with zeroes and hash it */
    if (index > 0) {
        memset(ctx->message + index, 0, 32 - index);
        rhash_gost_compute_sum_and_hash(ctx, msg32, nr);
    }

    /* hash the message length and the sum */
    msg32[0] = (unsigned)(ctx->length << 3);
    msg32[1] = (unsigned)(ctx->length >> 29);
    memset(msg32 + 2, 0, sizeof(unsigned)*6);

    rhash_gost_block_compress(ctx, msg32, nr);
    rhash_gost_block_compress(ctx, ctx->sum, nr);

    /* convert hash state to result bytes */
    le32_copy(result, 0, ctx->hash, gost_hash_length);
}

#ifdef GENERATE_GOST_LOOKUP_TABLE
unsigned rhash_gost_sbox[4][256];
unsigned rhash_gost_sbox_cryptpro[4][256];

/**
 * Calculate a lookup table from S-Boxes.
 * A substitution table is used to speed up hash calculation.
 *
 * @param out pointer to the lookup table to fill
 * @param src pointer to eight S-Boxes to fill the table from
 */
static void rhash_gost_fill_sbox(unsigned out[4][256], const unsigned char src[8][16])
{
	int a, b, i;
	unsigned long ax, bx, cx, dx;

	for (i = 0, a = 0; a < 16; a++) {
		ax = (unsigned)src[1][a] << 15;
		bx = (unsigned)src[3][a] << 23;
		cx = ROTL32((unsigned)src[5][a], 31);
		dx = (unsigned)src[7][a] << 7;

		for (b = 0; b < 16; b++, i++) {
			out[0][i] = ax | ((unsigned)src[0][b] << 11);
			out[1][i] = bx | ((unsigned)src[2][b] << 19);
			out[2][i] = cx | ((unsigned)src[4][b] << 27);
			out[3][i] = dx | ((unsigned)src[6][b] << 3);
		}
	}
}

/**
 * Initialize the GOST lookup tables for both parameters sets.
 * Two lookup tables contain 8 KiB in total, so calculating
 * them at rine-time can save a little space in the exutable file
 * in trade of consuming some time at pogram start.
 */
void rhash_gost_init_table(void)
{
	/* Test parameters set. Eight 4-bit S-Boxes defined by GOST R 34.10-94
	 * standard for testing the hash function.
	 * Also given by RFC 4357 section 11.2 */
	static const unsigned char sbox[8][16] = {
		{  4, 10,  9,  2, 13,  8,  0, 14,  6, 11,  1, 12,  7, 15,  5,  3 },
		{ 14, 11,  4, 12,  6, 13, 15, 10,  2,  3,  8,  1,  0,  7,  5,  9 },
		{  5,  8,  1, 13, 10,  3,  4,  2, 14, 15, 12,  7,  6,  0,  9, 11 },
		{  7, 13, 10,  1,  0,  8,  9, 15, 14,  4,  6, 12, 11,  2,  5,  3 },
		{  6, 12,  7,  1,  5, 15, 13,  8,  4, 10,  9, 14,  0,  3, 11,  2 },
		{  4, 11, 10,  0,  7,  2,  1, 13,  3,  6,  8,  5,  9, 12, 15, 14 },
		{ 13, 11,  4,  1,  3, 15,  5,  9,  0, 10, 14,  7,  6,  8,  2, 12 },
		{  1, 15, 13,  0,  5,  7, 10,  4,  9,  2,  3, 14,  6, 11,  8, 12 }
	};

	/* Parameter set recommended by RFC 4357.
	 * Eight 4-bit S-Boxes as defined by RFC 4357 section 11.2 */
	static const unsigned char sbox_cryptpro[8][16] = {
		{ 10,  4,  5,  6,  8,  1,  3,  7, 13, 12, 14,  0,  9,  2, 11, 15 },
		{  5, 15,  4,  0,  2, 13, 11,  9,  1,  7,  6,  3, 12, 14, 10,  8 },
		{  7, 15, 12, 14,  9,  4,  1,  0,  3, 11,  5,  2,  6, 10,  8, 13 },
		{  4, 10,  7, 12,  0, 15,  2,  8, 14,  1,  6,  5, 13, 11,  9,  3 },
		{  7,  6,  4, 11,  9, 12,  2, 10,  1,  8,  0, 14, 15, 13,  3,  5 },
		{  7,  6,  2,  4, 13,  9, 15,  0, 10,  1,  5, 11,  8, 14, 12,  3 },
		{ 13, 14,  4,  1,  7,  0,  5, 10,  3, 12,  8, 15,  6,  2,  9, 11 },
		{  1,  3, 10,  9,  5, 11,  4, 15,  8,  6,  7, 14, 13,  0,  2, 12 }
	};

	rhash_gost_fill_sbox(rhash_gost_sbox, sbox);
	rhash_gost_fill_sbox(rhash_gost_sbox_cryptpro, sbox_cryptpro);
}

#else /* GENERATE_GOST_LOOKUP_TABLE */

/* pre-initialized GOST lookup tables based on rotated S-Box */
unsigned rhash_gost_sbox[4][256] = {
    {
        0x72000, 0x75000, 0x74800, 0x71000, 0x76800,
                                           0x74000, 0x70000, 0x77000, 0x73000, 0x75800,
                                0x70800, 0x76000, 0x73800, 0x77800, 0x72800,
                                                                   0x71800, 0x5A000, 0x5D000, 0x5C800, 0x59000,
        0x5E800, 0x5C000, 0x58000, 0x5F000, 0x5B000,
                                           0x5D800, 0x58800, 0x5E000, 0x5B800, 0x5F800,
                                0x5A800, 0x59800, 0x22000, 0x25000, 0x24800,
                                                                   0x21000, 0x26800, 0x24000, 0x20000, 0x27000,
        0x23000, 0x25800, 0x20800, 0x26000, 0x23800,
                                           0x27800, 0x22800, 0x21800, 0x62000, 0x65000,
                                0x64800, 0x61000, 0x66800, 0x64000, 0x60000,
                                                                   0x67000, 0x63000, 0x65800, 0x60800, 0x66000,
        0x63800, 0x67800, 0x62800, 0x61800, 0x32000,
                                           0x35000, 0x34800, 0x31000, 0x36800, 0x34000,
                                0x30000, 0x37000, 0x33000, 0x35800, 0x30800,
                                                                   0x36000, 0x33800, 0x37800, 0x32800, 0x31800,
        0x6A000, 0x6D000, 0x6C800, 0x69000, 0x6E800,
                                           0x6C000, 0x68000, 0x6F000, 0x6B000, 0x6D800,
                                0x68800, 0x6E000, 0x6B800, 0x6F800, 0x6A800,
                                                                   0x69800, 0x7A000, 0x7D000, 0x7C800, 0x79000,
        0x7E800, 0x7C000, 0x78000, 0x7F000, 0x7B000,
                                           0x7D800, 0x78800, 0x7E000, 0x7B800, 0x7F800,
                                0x7A800, 0x79800, 0x52000, 0x55000, 0x54800,
                                                                   0x51000, 0x56800, 0x54000, 0x50000, 0x57000,
        0x53000, 0x55800, 0x50800, 0x56000, 0x53800,
                                           0x57800, 0x52800, 0x51800, 0x12000, 0x15000,
                                0x14800, 0x11000, 0x16800, 0x14000, 0x10000,
                                                                   0x17000, 0x13000, 0x15800, 0x10800, 0x16000,
        0x13800, 0x17800, 0x12800, 0x11800, 0x1A000,
                                           0x1D000, 0x1C800, 0x19000, 0x1E800, 0x1C000,
                                0x18000, 0x1F000, 0x1B000, 0x1D800, 0x18800,
                                                                   0x1E000, 0x1B800, 0x1F800, 0x1A800, 0x19800,
        0x42000, 0x45000, 0x44800, 0x41000, 0x46800,
                                           0x44000, 0x40000, 0x47000, 0x43000, 0x45800,
                                0x40800, 0x46000, 0x43800, 0x47800, 0x42800,
                                                                   0x41800, 0xA000,  0xD000,  0xC800,  0x9000,
        0xE800,  0xC000,  0x8000,  0xF000,  0xB000,
                                           0xD800,  0x8800,  0xE000,  0xB800,  0xF800,
                                0xA800,  0x9800,  0x2000,  0x5000,  0x4800,
                                                                   0x1000,  0x6800,  0x4000,  0x0,     0x7000,
        0x3000,  0x5800,  0x800,   0x6000,  0x3800,
                                           0x7800,  0x2800,  0x1800,  0x3A000, 0x3D000,
                                0x3C800, 0x39000, 0x3E800, 0x3C000, 0x38000,
                                                                   0x3F000, 0x3B000, 0x3D800, 0x38800, 0x3E000,
        0x3B800, 0x3F800, 0x3A800, 0x39800, 0x2A000,
                                           0x2D000, 0x2C800, 0x29000, 0x2E800, 0x2C000,
                                0x28000, 0x2F000, 0x2B000, 0x2D800, 0x28800,
                                                                   0x2E000, 0x2B800, 0x2F800, 0x2A800, 0x29800,
        0x4A000, 0x4D000, 0x4C800, 0x49000, 0x4E800,
                                           0x4C000, 0x48000, 0x4F000, 0x4B000, 0x4D800,
                                0x48800, 0x4E000, 0x4B800, 0x4F800, 0x4A800,
                                                                   0x49800
    }, {
        0x3A80000, 0x3C00000, 0x3880000, 0x3E80000, 0x3D00000,
                                           0x3980000, 0x3A00000, 0x3900000, 0x3F00000, 0x3F80000,
                                0x3E00000, 0x3B80000, 0x3B00000, 0x3800000, 0x3C80000,
                                                                   0x3D80000, 0x6A80000, 0x6C00000, 0x6880000, 0x6E80000,
        0x6D00000, 0x6980000, 0x6A00000, 0x6900000, 0x6F00000,
                                           0x6F80000, 0x6E00000, 0x6B80000, 0x6B00000, 0x6800000,
                                0x6C80000, 0x6D80000, 0x5280000, 0x5400000, 0x5080000,
                                                                   0x5680000, 0x5500000, 0x5180000, 0x5200000, 0x5100000,
        0x5700000, 0x5780000, 0x5600000, 0x5380000, 0x5300000,
                                           0x5000000, 0x5480000, 0x5580000, 0xA80000,  0xC00000,
                                0x880000,  0xE80000,  0xD00000,  0x980000,  0xA00000,
                                                                   0x900000,  0xF00000,  0xF80000,  0xE00000,  0xB80000,
        0xB00000,  0x800000,  0xC80000,  0xD80000,  0x280000,
                                           0x400000,  0x80000,   0x680000,  0x500000,  0x180000,
                                0x200000,  0x100000,  0x700000,  0x780000,  0x600000,
                                                                   0x380000,  0x300000,  0x0,       0x480000,  0x580000,
        0x4280000, 0x4400000, 0x4080000, 0x4680000, 0x4500000,
                                           0x4180000, 0x4200000, 0x4100000, 0x4700000, 0x4780000,
                                0x4600000, 0x4380000, 0x4300000, 0x4000000, 0x4480000,
                                                                   0x4580000, 0x4A80000, 0x4C00000, 0x4880000, 0x4E80000,
        0x4D00000, 0x4980000, 0x4A00000, 0x4900000, 0x4F00000,
                                           0x4F80000, 0x4E00000, 0x4B80000, 0x4B00000, 0x4800000,
                                0x4C80000, 0x4D80000, 0x7A80000, 0x7C00000, 0x7880000,
                                                                   0x7E80000, 0x7D00000, 0x7980000, 0x7A00000, 0x7900000,
        0x7F00000, 0x7F80000, 0x7E00000, 0x7B80000, 0x7B00000,
                                           0x7800000, 0x7C80000, 0x7D80000, 0x7280000, 0x7400000,
                                0x7080000, 0x7680000, 0x7500000, 0x7180000, 0x7200000,
                                                                   0x7100000, 0x7700000, 0x7780000, 0x7600000, 0x7380000,
        0x7300000, 0x7000000, 0x7480000, 0x7580000, 0x2280000,
                                           0x2400000, 0x2080000, 0x2680000, 0x2500000, 0x2180000,
                                0x2200000, 0x2100000, 0x2700000, 0x2780000, 0x2600000,
                                                                   0x2380000, 0x2300000, 0x2000000, 0x2480000, 0x2580000,
        0x3280000, 0x3400000, 0x3080000, 0x3680000, 0x3500000,
                                           0x3180000, 0x3200000, 0x3100000, 0x3700000, 0x3780000,
                                0x3600000, 0x3380000, 0x3300000, 0x3000000, 0x3480000,
                                                                   0x3580000, 0x6280000, 0x6400000, 0x6080000, 0x6680000,
        0x6500000, 0x6180000, 0x6200000, 0x6100000, 0x6700000,
                                           0x6780000, 0x6600000, 0x6380000, 0x6300000, 0x6000000,
                                0x6480000, 0x6580000, 0x5A80000, 0x5C00000, 0x5880000,
                                                                   0x5E80000, 0x5D00000, 0x5980000, 0x5A00000, 0x5900000,
        0x5F00000, 0x5F80000, 0x5E00000, 0x5B80000, 0x5B00000,
                                           0x5800000, 0x5C80000, 0x5D80000, 0x1280000, 0x1400000,
                                0x1080000, 0x1680000, 0x1500000, 0x1180000, 0x1200000,
                                                                   0x1100000, 0x1700000, 0x1780000, 0x1600000, 0x1380000,
        0x1300000, 0x1000000, 0x1480000, 0x1580000, 0x2A80000,
                                           0x2C00000, 0x2880000, 0x2E80000, 0x2D00000, 0x2980000,
                                0x2A00000, 0x2900000, 0x2F00000, 0x2F80000, 0x2E00000,
                                                                   0x2B80000, 0x2B00000, 0x2800000, 0x2C80000, 0x2D80000,
        0x1A80000, 0x1C00000, 0x1880000, 0x1E80000, 0x1D00000,
                                           0x1980000, 0x1A00000, 0x1900000, 0x1F00000, 0x1F80000,
                                0x1E00000, 0x1B80000, 0x1B00000, 0x1800000, 0x1C80000,
                                                                   0x1D80000
    }, {
        0x30000002, 0x60000002, 0x38000002, 0x8000002,
        0x28000002, 0x78000002, 0x68000002, 0x40000002,
        0x20000002, 0x50000002, 0x48000002, 0x70000002,
        0x2,        0x18000002, 0x58000002, 0x10000002,
        0xB0000005, 0xE0000005, 0xB8000005, 0x88000005,
        0xA8000005, 0xF8000005, 0xE8000005, 0xC0000005,
        0xA0000005, 0xD0000005, 0xC8000005, 0xF0000005,
        0x80000005, 0x98000005, 0xD8000005, 0x90000005,
        0x30000005, 0x60000005, 0x38000005, 0x8000005,
        0x28000005, 0x78000005, 0x68000005, 0x40000005,
        0x20000005, 0x50000005, 0x48000005, 0x70000005,
        0x5,        0x18000005, 0x58000005, 0x10000005,
        0x30000000, 0x60000000, 0x38000000, 0x8000000,
        0x28000000, 0x78000000, 0x68000000, 0x40000000,
        0x20000000, 0x50000000, 0x48000000, 0x70000000,
        0x0,        0x18000000, 0x58000000, 0x10000000,
        0xB0000003, 0xE0000003, 0xB8000003, 0x88000003,
        0xA8000003, 0xF8000003, 0xE8000003, 0xC0000003,
        0xA0000003, 0xD0000003, 0xC8000003, 0xF0000003,
        0x80000003, 0x98000003, 0xD8000003, 0x90000003,
        0x30000001, 0x60000001, 0x38000001, 0x8000001,
        0x28000001, 0x78000001, 0x68000001, 0x40000001,
        0x20000001, 0x50000001, 0x48000001, 0x70000001,
        0x1,        0x18000001, 0x58000001, 0x10000001,
        0xB0000000, 0xE0000000, 0xB8000000, 0x88000000,
        0xA8000000, 0xF8000000, 0xE8000000, 0xC0000000,
        0xA0000000, 0xD0000000, 0xC8000000, 0xF0000000,
        0x80000000, 0x98000000, 0xD8000000, 0x90000000,
        0xB0000006, 0xE0000006, 0xB8000006, 0x88000006,
        0xA8000006, 0xF8000006, 0xE8000006, 0xC0000006,
        0xA0000006, 0xD0000006, 0xC8000006, 0xF0000006,
        0x80000006, 0x98000006, 0xD8000006, 0x90000006,
        0xB0000001, 0xE0000001, 0xB8000001, 0x88000001,
        0xA8000001, 0xF8000001, 0xE8000001, 0xC0000001,
        0xA0000001, 0xD0000001, 0xC8000001, 0xF0000001,
        0x80000001, 0x98000001, 0xD8000001, 0x90000001,
        0x30000003, 0x60000003, 0x38000003, 0x8000003,
        0x28000003, 0x78000003, 0x68000003, 0x40000003,
        0x20000003, 0x50000003, 0x48000003, 0x70000003,
        0x3,        0x18000003, 0x58000003, 0x10000003,
        0x30000004, 0x60000004, 0x38000004, 0x8000004,
        0x28000004, 0x78000004, 0x68000004, 0x40000004,
        0x20000004, 0x50000004, 0x48000004, 0x70000004,
        0x4,        0x18000004, 0x58000004, 0x10000004,
        0xB0000002, 0xE0000002, 0xB8000002, 0x88000002,
        0xA8000002, 0xF8000002, 0xE8000002, 0xC0000002,
        0xA0000002, 0xD0000002, 0xC8000002, 0xF0000002,
        0x80000002, 0x98000002, 0xD8000002, 0x90000002,
        0xB0000004, 0xE0000004, 0xB8000004, 0x88000004,
        0xA8000004, 0xF8000004, 0xE8000004, 0xC0000004,
        0xA0000004, 0xD0000004, 0xC8000004, 0xF0000004,
        0x80000004, 0x98000004, 0xD8000004, 0x90000004,
        0x30000006, 0x60000006, 0x38000006, 0x8000006,
        0x28000006, 0x78000006, 0x68000006, 0x40000006,
        0x20000006, 0x50000006, 0x48000006, 0x70000006,
        0x6,        0x18000006, 0x58000006, 0x10000006,
        0xB0000007, 0xE0000007, 0xB8000007, 0x88000007,
        0xA8000007, 0xF8000007, 0xE8000007, 0xC0000007,
        0xA0000007, 0xD0000007, 0xC8000007, 0xF0000007,
        0x80000007, 0x98000007, 0xD8000007, 0x90000007,
        0x30000007, 0x60000007, 0x38000007, 0x8000007,
        0x28000007, 0x78000007, 0x68000007, 0x40000007,
        0x20000007, 0x50000007, 0x48000007, 0x70000007,
        0x7,        0x18000007, 0x58000007, 0x10000007
    }, {
        0xE8,  0xD8,  0xA0,  0x88,  0x98,  0xF8,  0xA8,  0xC8,  0x80,  0xD0,
                                0xF0,  0xB8,  0xB0,  0xC0,  0x90,  0xE0,  0x7E8, 0x7D8, 0x7A0, 0x788,
        0x798, 0x7F8, 0x7A8, 0x7C8, 0x780, 0x7D0, 0x7F0, 0x7B8, 0x7B0, 0x7C0,
                                0x790, 0x7E0, 0x6E8, 0x6D8, 0x6A0, 0x688, 0x698, 0x6F8, 0x6A8, 0x6C8,
        0x680, 0x6D0, 0x6F0, 0x6B8, 0x6B0, 0x6C0, 0x690, 0x6E0, 0x68,  0x58,
                                0x20,  0x8,   0x18,  0x78,  0x28,  0x48,  0x0,   0x50,  0x70,  0x38,
        0x30,  0x40,  0x10,  0x60,  0x2E8, 0x2D8, 0x2A0, 0x288, 0x298, 0x2F8,
                                0x2A8, 0x2C8, 0x280, 0x2D0, 0x2F0, 0x2B8, 0x2B0, 0x2C0, 0x290, 0x2E0,
        0x3E8, 0x3D8, 0x3A0, 0x388, 0x398, 0x3F8, 0x3A8, 0x3C8, 0x380, 0x3D0,
                                0x3F0, 0x3B8, 0x3B0, 0x3C0, 0x390, 0x3E0, 0x568, 0x558, 0x520, 0x508,
        0x518, 0x578, 0x528, 0x548, 0x500, 0x550, 0x570, 0x538, 0x530, 0x540,
                                0x510, 0x560, 0x268, 0x258, 0x220, 0x208, 0x218, 0x278, 0x228, 0x248,
        0x200, 0x250, 0x270, 0x238, 0x230, 0x240, 0x210, 0x260, 0x4E8, 0x4D8,
                                0x4A0, 0x488, 0x498, 0x4F8, 0x4A8, 0x4C8, 0x480, 0x4D0, 0x4F0, 0x4B8,
        0x4B0, 0x4C0, 0x490, 0x4E0, 0x168, 0x158, 0x120, 0x108, 0x118, 0x178,
                                0x128, 0x148, 0x100, 0x150, 0x170, 0x138, 0x130, 0x140, 0x110, 0x160,
        0x1E8, 0x1D8, 0x1A0, 0x188, 0x198, 0x1F8, 0x1A8, 0x1C8, 0x180, 0x1D0,
                                0x1F0, 0x1B8, 0x1B0, 0x1C0, 0x190, 0x1E0, 0x768, 0x758, 0x720, 0x708,
        0x718, 0x778, 0x728, 0x748, 0x700, 0x750, 0x770, 0x738, 0x730, 0x740,
                                0x710, 0x760, 0x368, 0x358, 0x320, 0x308, 0x318, 0x378, 0x328, 0x348,
        0x300, 0x350, 0x370, 0x338, 0x330, 0x340, 0x310, 0x360, 0x5E8, 0x5D8,
                                0x5A0, 0x588, 0x598, 0x5F8, 0x5A8, 0x5C8, 0x580, 0x5D0, 0x5F0, 0x5B8,
        0x5B0, 0x5C0, 0x590, 0x5E0, 0x468, 0x458, 0x420, 0x408, 0x418, 0x478,
                                0x428, 0x448, 0x400, 0x450, 0x470, 0x438, 0x430, 0x440, 0x410, 0x460,
        0x668, 0x658, 0x620, 0x608, 0x618, 0x678, 0x628, 0x648, 0x600, 0x650,
                                0x670, 0x638, 0x630, 0x640, 0x610, 0x660
    }
};

/* pre-initialized GOST lookup tables based on rotated S-Box */
unsigned rhash_gost_sbox_cryptpro[4][256] = {
    {
        0x2d000, 0x2a000, 0x2a800, 0x2b000, 0x2c000,
                                           0x28800, 0x29800, 0x2b800, 0x2e800, 0x2e000,
        0x2f000, 0x28000, 0x2c800, 0x29000, 0x2d800,
                                           0x2f800, 0x7d000, 0x7a000, 0x7a800, 0x7b000,
        0x7c000, 0x78800, 0x79800, 0x7b800, 0x7e800,
                                           0x7e000, 0x7f000, 0x78000, 0x7c800, 0x79000,
        0x7d800, 0x7f800, 0x25000, 0x22000, 0x22800,
                                           0x23000, 0x24000, 0x20800, 0x21800, 0x23800,
        0x26800, 0x26000, 0x27000, 0x20000, 0x24800,
                                           0x21000, 0x25800, 0x27800, 0x5000,  0x2000,
        0x2800,  0x3000,  0x4000,  0x800,   0x1800,
                                           0x3800,  0x6800,  0x6000,  0x7000,  0x0,
        0x4800,  0x1000,  0x5800,  0x7800,  0x15000,
                                           0x12000, 0x12800, 0x13000, 0x14000, 0x10800,
        0x11800, 0x13800, 0x16800, 0x16000, 0x17000,
                                           0x10000, 0x14800, 0x11000, 0x15800, 0x17800,
        0x6d000, 0x6a000, 0x6a800, 0x6b000, 0x6c000,
                                           0x68800, 0x69800, 0x6b800, 0x6e800, 0x6e000,
        0x6f000, 0x68000, 0x6c800, 0x69000, 0x6d800,
                                           0x6f800, 0x5d000, 0x5a000, 0x5a800, 0x5b000,
        0x5c000, 0x58800, 0x59800, 0x5b800, 0x5e800,
                                           0x5e000, 0x5f000, 0x58000, 0x5c800, 0x59000,
        0x5d800, 0x5f800, 0x4d000, 0x4a000, 0x4a800,
                                           0x4b000, 0x4c000, 0x48800, 0x49800, 0x4b800,
        0x4e800, 0x4e000, 0x4f000, 0x48000, 0x4c800,
                                           0x49000, 0x4d800, 0x4f800, 0xd000,  0xa000,
        0xa800,  0xb000,  0xc000,  0x8800,  0x9800,
                                           0xb800,  0xe800,  0xe000,  0xf000,  0x8000,
        0xc800,  0x9000,  0xd800,  0xf800,  0x3d000,
                                           0x3a000, 0x3a800, 0x3b000, 0x3c000, 0x38800,
        0x39800, 0x3b800, 0x3e800, 0x3e000, 0x3f000,
                                           0x38000, 0x3c800, 0x39000, 0x3d800, 0x3f800,
        0x35000, 0x32000, 0x32800, 0x33000, 0x34000,
                                           0x30800, 0x31800, 0x33800, 0x36800, 0x36000,
        0x37000, 0x30000, 0x34800, 0x31000, 0x35800,
                                           0x37800, 0x1d000, 0x1a000, 0x1a800, 0x1b000,
        0x1c000, 0x18800, 0x19800, 0x1b800, 0x1e800,
                                           0x1e000, 0x1f000, 0x18000, 0x1c800, 0x19000,
        0x1d800, 0x1f800, 0x65000, 0x62000, 0x62800,
                                           0x63000, 0x64000, 0x60800, 0x61800, 0x63800,
        0x66800, 0x66000, 0x67000, 0x60000, 0x64800,
                                           0x61000, 0x65800, 0x67800, 0x75000, 0x72000,
        0x72800, 0x73000, 0x74000, 0x70800, 0x71800,
                                           0x73800, 0x76800, 0x76000, 0x77000, 0x70000,
        0x74800, 0x71000, 0x75800, 0x77800, 0x55000,
                                           0x52000, 0x52800, 0x53000, 0x54000, 0x50800,
        0x51800, 0x53800, 0x56800, 0x56000, 0x57000,
                                           0x50000, 0x54800, 0x51000, 0x55800, 0x57800,
        0x45000, 0x42000, 0x42800, 0x43000, 0x44000,
                                           0x40800, 0x41800, 0x43800, 0x46800, 0x46000,
        0x47000, 0x40000, 0x44800, 0x41000, 0x45800, 0x47800
    }, {
        0x2380000, 0x2780000, 0x2600000, 0x2700000, 0x2480000,
                                           0x2200000, 0x2080000, 0x2000000, 0x2180000, 0x2580000,
        0x2280000, 0x2100000, 0x2300000, 0x2500000, 0x2400000,
                                           0x2680000, 0x5380000, 0x5780000, 0x5600000, 0x5700000,
        0x5480000, 0x5200000, 0x5080000, 0x5000000, 0x5180000,
                                           0x5580000, 0x5280000, 0x5100000, 0x5300000, 0x5500000,
        0x5400000, 0x5680000, 0x3b80000, 0x3f80000, 0x3e00000,
                                           0x3f00000, 0x3c80000, 0x3a00000, 0x3880000, 0x3800000,
        0x3980000, 0x3d80000, 0x3a80000, 0x3900000, 0x3b00000,
                                           0x3d00000, 0x3c00000, 0x3e80000, 0x6380000, 0x6780000,
        0x6600000, 0x6700000, 0x6480000, 0x6200000, 0x6080000,
                                           0x6000000, 0x6180000, 0x6580000, 0x6280000, 0x6100000,
        0x6300000, 0x6500000, 0x6400000, 0x6680000, 0x380000,
                                           0x780000,  0x600000,  0x700000,  0x480000,  0x200000,
        0x80000,   0x0,       0x180000,  0x580000,  0x280000,
                                           0x100000,  0x300000,  0x500000,  0x400000,  0x680000,
        0x7b80000, 0x7f80000, 0x7e00000, 0x7f00000, 0x7c80000,
                                           0x7a00000, 0x7880000, 0x7800000, 0x7980000, 0x7d80000,
        0x7a80000, 0x7900000, 0x7b00000, 0x7d00000, 0x7c00000,
                                           0x7e80000, 0x1380000, 0x1780000, 0x1600000, 0x1700000,
        0x1480000, 0x1200000, 0x1080000, 0x1000000, 0x1180000,
                                           0x1580000, 0x1280000, 0x1100000, 0x1300000, 0x1500000,
        0x1400000, 0x1680000, 0x4380000, 0x4780000, 0x4600000,
                                           0x4700000, 0x4480000, 0x4200000, 0x4080000, 0x4000000,
        0x4180000, 0x4580000, 0x4280000, 0x4100000, 0x4300000,
                                           0x4500000, 0x4400000, 0x4680000, 0x7380000, 0x7780000,
        0x7600000, 0x7700000, 0x7480000, 0x7200000, 0x7080000,
                                           0x7000000, 0x7180000, 0x7580000, 0x7280000, 0x7100000,
        0x7300000, 0x7500000, 0x7400000, 0x7680000, 0xb80000,
                                           0xf80000,  0xe00000,  0xf00000,  0xc80000,  0xa00000,
        0x880000,  0x800000,  0x980000,  0xd80000,  0xa80000,
                                           0x900000,  0xb00000,  0xd00000,  0xc00000,  0xe80000,
        0x3380000, 0x3780000, 0x3600000, 0x3700000, 0x3480000,
                                           0x3200000, 0x3080000, 0x3000000, 0x3180000, 0x3580000,
        0x3280000, 0x3100000, 0x3300000, 0x3500000, 0x3400000,
                                           0x3680000, 0x2b80000, 0x2f80000, 0x2e00000, 0x2f00000,
        0x2c80000, 0x2a00000, 0x2880000, 0x2800000, 0x2980000,
                                           0x2d80000, 0x2a80000, 0x2900000, 0x2b00000, 0x2d00000,
        0x2c00000, 0x2e80000, 0x6b80000, 0x6f80000, 0x6e00000,
                                           0x6f00000, 0x6c80000, 0x6a00000, 0x6880000, 0x6800000,
        0x6980000, 0x6d80000, 0x6a80000, 0x6900000, 0x6b00000,
                                           0x6d00000, 0x6c00000, 0x6e80000, 0x5b80000, 0x5f80000,
        0x5e00000, 0x5f00000, 0x5c80000, 0x5a00000, 0x5880000,
                                           0x5800000, 0x5980000, 0x5d80000, 0x5a80000, 0x5900000,
        0x5b00000, 0x5d00000, 0x5c00000, 0x5e80000, 0x4b80000,
                                           0x4f80000, 0x4e00000, 0x4f00000, 0x4c80000, 0x4a00000,
        0x4880000, 0x4800000, 0x4980000, 0x4d80000, 0x4a80000,
                                           0x4900000, 0x4b00000, 0x4d00000, 0x4c00000, 0x4e80000,
        0x1b80000, 0x1f80000, 0x1e00000, 0x1f00000, 0x1c80000,
                                           0x1a00000, 0x1880000, 0x1800000, 0x1980000, 0x1d80000,
        0x1a80000, 0x1900000, 0x1b00000, 0x1d00000, 0x1c00000,
                                                     0x1e80000
    }, {
        0xb8000003, 0xb0000003, 0xa0000003, 0xd8000003, 0xc8000003,
                                           0xe0000003, 0x90000003, 0xd0000003, 0x88000003, 0xc0000003,
        0x80000003, 0xf0000003, 0xf8000003, 0xe8000003, 0x98000003,
                                           0xa8000003, 0x38000003, 0x30000003, 0x20000003, 0x58000003,
        0x48000003, 0x60000003, 0x10000003, 0x50000003, 0x8000003,
                                           0x40000003, 0x3,        0x70000003, 0x78000003, 0x68000003,
        0x18000003, 0x28000003, 0x38000001, 0x30000001, 0x20000001,
                                           0x58000001, 0x48000001, 0x60000001, 0x10000001, 0x50000001,
        0x8000001,  0x40000001, 0x1,        0x70000001, 0x78000001,
                                           0x68000001, 0x18000001, 0x28000001, 0x38000002, 0x30000002,
        0x20000002, 0x58000002, 0x48000002, 0x60000002, 0x10000002,
                                           0x50000002, 0x8000002,  0x40000002, 0x2,        0x70000002,
        0x78000002, 0x68000002, 0x18000002, 0x28000002, 0xb8000006,
                                           0xb0000006, 0xa0000006, 0xd8000006, 0xc8000006, 0xe0000006,
        0x90000006, 0xd0000006, 0x88000006, 0xc0000006, 0x80000006,
                                           0xf0000006, 0xf8000006, 0xe8000006, 0x98000006, 0xa8000006,
        0xb8000004, 0xb0000004, 0xa0000004, 0xd8000004, 0xc8000004,
                                           0xe0000004, 0x90000004, 0xd0000004, 0x88000004, 0xc0000004,
        0x80000004, 0xf0000004, 0xf8000004, 0xe8000004, 0x98000004,
                                           0xa8000004, 0xb8000007, 0xb0000007, 0xa0000007, 0xd8000007,
        0xc8000007, 0xe0000007, 0x90000007, 0xd0000007, 0x88000007,
                                           0xc0000007, 0x80000007, 0xf0000007, 0xf8000007, 0xe8000007,
        0x98000007, 0xa8000007, 0x38000000, 0x30000000, 0x20000000,
                                           0x58000000, 0x48000000, 0x60000000, 0x10000000, 0x50000000,
        0x8000000,  0x40000000, 0x0,        0x70000000, 0x78000000,
                                           0x68000000, 0x18000000, 0x28000000, 0x38000005, 0x30000005,
        0x20000005, 0x58000005, 0x48000005, 0x60000005, 0x10000005,
                                           0x50000005, 0x8000005,  0x40000005, 0x5,        0x70000005,
        0x78000005, 0x68000005, 0x18000005, 0x28000005, 0xb8000000,
                                           0xb0000000, 0xa0000000, 0xd8000000, 0xc8000000, 0xe0000000,
        0x90000000, 0xd0000000, 0x88000000, 0xc0000000, 0x80000000,
                                           0xf0000000, 0xf8000000, 0xe8000000, 0x98000000, 0xa8000000,
        0xb8000002, 0xb0000002, 0xa0000002, 0xd8000002, 0xc8000002,
                                           0xe0000002, 0x90000002, 0xd0000002, 0x88000002, 0xc0000002,
        0x80000002, 0xf0000002, 0xf8000002, 0xe8000002, 0x98000002,
                                           0xa8000002, 0xb8000005, 0xb0000005, 0xa0000005, 0xd8000005,
        0xc8000005, 0xe0000005, 0x90000005, 0xd0000005, 0x88000005,
                                           0xc0000005, 0x80000005, 0xf0000005, 0xf8000005, 0xe8000005,
        0x98000005, 0xa8000005, 0x38000004, 0x30000004, 0x20000004,
                                           0x58000004, 0x48000004, 0x60000004, 0x10000004, 0x50000004,
        0x8000004,  0x40000004, 0x4,        0x70000004, 0x78000004,
                                           0x68000004, 0x18000004, 0x28000004, 0x38000007, 0x30000007,
        0x20000007, 0x58000007, 0x48000007, 0x60000007, 0x10000007,
                                           0x50000007, 0x8000007,  0x40000007, 0x7,        0x70000007,
        0x78000007, 0x68000007, 0x18000007, 0x28000007, 0x38000006,
                                           0x30000006, 0x20000006, 0x58000006, 0x48000006, 0x60000006,
        0x10000006, 0x50000006, 0x8000006,  0x40000006, 0x6,
                                           0x70000006, 0x78000006, 0x68000006, 0x18000006, 0x28000006,
        0xb8000001, 0xb0000001, 0xa0000001, 0xd8000001, 0xc8000001,
                                           0xe0000001, 0x90000001, 0xd0000001, 0x88000001, 0xc0000001,
        0x80000001, 0xf0000001, 0xf8000001, 0xe8000001, 0x98000001,
                                                     0xa8000001
    }, {
        0xe8,  0xf0,  0xa0,  0x88,  0xb8,  0x80,  0xa8,  0xd0,  0x98,  0xe0,
        0xc0,  0xf8,  0xb0,  0x90,  0xc8,  0xd8,  0x1e8, 0x1f0, 0x1a0, 0x188,
        0x1b8, 0x180, 0x1a8, 0x1d0, 0x198, 0x1e0, 0x1c0, 0x1f8, 0x1b0, 0x190,
        0x1c8, 0x1d8, 0x568, 0x570, 0x520, 0x508, 0x538, 0x500, 0x528, 0x550,
        0x518, 0x560, 0x540, 0x578, 0x530, 0x510, 0x548, 0x558, 0x4e8, 0x4f0,
        0x4a0, 0x488, 0x4b8, 0x480, 0x4a8, 0x4d0, 0x498, 0x4e0, 0x4c0, 0x4f8,
        0x4b0, 0x490, 0x4c8, 0x4d8, 0x2e8, 0x2f0, 0x2a0, 0x288, 0x2b8, 0x280,
        0x2a8, 0x2d0, 0x298, 0x2e0, 0x2c0, 0x2f8, 0x2b0, 0x290, 0x2c8, 0x2d8,
        0x5e8, 0x5f0, 0x5a0, 0x588, 0x5b8, 0x580, 0x5a8, 0x5d0, 0x598, 0x5e0,
        0x5c0, 0x5f8, 0x5b0, 0x590, 0x5c8, 0x5d8, 0x268, 0x270, 0x220, 0x208,
        0x238, 0x200, 0x228, 0x250, 0x218, 0x260, 0x240, 0x278, 0x230, 0x210,
        0x248, 0x258, 0x7e8, 0x7f0, 0x7a0, 0x788, 0x7b8, 0x780, 0x7a8, 0x7d0,
        0x798, 0x7e0, 0x7c0, 0x7f8, 0x7b0, 0x790, 0x7c8, 0x7d8, 0x468, 0x470,
        0x420, 0x408, 0x438, 0x400, 0x428, 0x450, 0x418, 0x460, 0x440, 0x478,
        0x430, 0x410, 0x448, 0x458, 0x368, 0x370, 0x320, 0x308, 0x338, 0x300,
        0x328, 0x350, 0x318, 0x360, 0x340, 0x378, 0x330, 0x310, 0x348, 0x358,
        0x3e8, 0x3f0, 0x3a0, 0x388, 0x3b8, 0x380, 0x3a8, 0x3d0, 0x398, 0x3e0,
        0x3c0, 0x3f8, 0x3b0, 0x390, 0x3c8, 0x3d8, 0x768, 0x770, 0x720, 0x708,
        0x738, 0x700, 0x728, 0x750, 0x718, 0x760, 0x740, 0x778, 0x730, 0x710,
        0x748, 0x758, 0x6e8, 0x6f0, 0x6a0, 0x688, 0x6b8, 0x680, 0x6a8, 0x6d0,
        0x698, 0x6e0, 0x6c0, 0x6f8, 0x6b0, 0x690, 0x6c8, 0x6d8, 0x68,  0x70,
        0x20,  0x8,   0x38,  0x0,   0x28,  0x50,  0x18,  0x60,  0x40,  0x78,
        0x30,  0x10,  0x48,  0x58,  0x168, 0x170, 0x120, 0x108, 0x138, 0x100,
        0x128, 0x150, 0x118, 0x160, 0x140, 0x178, 0x130, 0x110, 0x148, 0x158,
        0x668, 0x670, 0x620, 0x608, 0x638, 0x600, 0x628, 0x650, 0x618, 0x660,
        0x640, 0x678, 0x630, 0x610, 0x648, 0x658
    }
};


#endif /* GENERATE_GOST_LOOKUP_TABLE */
