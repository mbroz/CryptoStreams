/* ecrypt-sync.h */

/*
 * Header file for synchronous stream ciphers without authentication
 * mechanism.
 *
 * *** Please only edit parts marked with "[edit]". ***
 */

#ifndef ABC_SYNC
#define ABC_SYNC

#include "../../stream_interface.h"
#include "../ecrypt-portable.h"

namespace stream_ciphers {
namespace estream {

/* ------------------------------------------------------------------------- */

/* Cipher parameters */

/*
 * The name of your cipher.
 */
#define ABC_NAME "ABC-v3" /* [edit] */
#define ABC_PROFILE "_____"

/*
 * Specify which key and IV sizes are supported by your cipher. A user
 * should be able to enumerate the supported sizes by running the
 * following code:
 *
 * for (i = 0; ABC_KEYSIZE(i) <= ABC_MAXKEYSIZE; ++i)
 *   {
 *     keysize = ABC_KEYSIZE(i);
 *
 *     ...
 *   }
 *
 * All sizes are in bits.
 */

#define ABC_MAXKEYSIZE 128            /* ABC maximum key size             */
#define ABC_KEYSIZE(i) (1 << (7 + i)) /* Key size                         */

#define ABC_MAXIVSIZE 128            /* ABC maximum IV size              */
#define ABC_IVSIZE(i) (1 << (7 + i)) /* IV size                          */

/* ------------------------------------------------------------------------- */

/* Data structures */

/*
 * ABC_ctx is the structure containing the representation of the
 * internal state of ABC cipher.
 */

/*
 * NOTE: ABC_VARIANT is not supposed to affect the external
 * interface in any way. The definitions below violate this rule, but
 * since the test suite does not rely on it at this stage, we leave it
 * this way.
 */

typedef struct {
    u32 z0, z1, z2, z3;     /* A primitive (LFSR) state                   */
    u32 x;                  /* B primitive (top function) state           */
    u32 d0, d1, d2;         /* B primitive coefficients                   */
    u32 z0i, z1i, z2i, z3i; /* A state right after key setup          */
    u32 xi;                 /* B state right after key setup              */
    u32 d0i, d1i, d2i;      /* B coefs after key setup                    */
#if (ABC_VARIANT >= 10)
    u32 t[33]; /* C primitive coefficients                   */
#elif (ABC_VARIANT >= 8)
    u32 t[64]; /* Optimization table for 2-bit window        */
#elif (ABC_VARIANT >= 5)
    u32 t[128]; /* Optimization table for 4-bit window        */
#elif (ABC_VARIANT >= 2)
    u32 t[1024]; /* Optimization table for 8-bit window        */
#elif (ABC_VARIANT >= 1) || !defined(ABC_VARIANT)
    u32 t[8448]; /* Optimization table for 12-12-8-bit windows */
#endif
} ABC_ctx;

/* ------------------------------------------------------------------------- */
class ECRYPT_ABC : public estream_interface {
    ABC_ctx _ctx;

public:
    /* Mandatory functions */

    /*
     * Key and message independent initialization. This function will be
     * called once when the program starts (e.g., to build expanded S-box
     * tables).
     */
    void ECRYPT_init(void) override;

    /*
     * Key setup. It is the user's responsibility to select the values of
     * keysize and ivsize from the set of supported values specified
     * above.
     */
    void ECRYPT_keysetup(const u8* key,
                         u32 keysize,          /* Key size in bits. */
                         u32 ivsize) override; /* IV size in bits. */

    /*
     * IV setup. After having called ECRYPT_keysetup(), the user is
     * allowed to call ECRYPT_ivsetup() different times in order to
     * encrypt/decrypt different messages with the same key but different
     * IV's.
     */
    void ECRYPT_ivsetup(const u8* iv) override;

/*
 * Encryption/decryption of arbitrary length messages.
 *
 * For efficiency reasons, the API provides two types of
 * encrypt/decrypt functions. The ECRYPT_encrypt_bytes() function
 * (declared here) encrypts byte strings of arbitrary length, while
 * the ECRYPT_encrypt_blocks() function (defined later) only accepts
 * lengths which are multiples of ECRYPT_BLOCKLENGTH.
 *
 * The user is allowed to make multiple calls to
 * ECRYPT_encrypt_blocks() to incrementally encrypt a long message,
 * but he is NOT allowed to make additional encryption calls once he
 * has called ECRYPT_encrypt_bytes() (unless he starts a new message
 * of course). For example, this sequence of calls is acceptable:
 *
 * ECRYPT_keysetup();
 *
 * ECRYPT_ivsetup();
 * ECRYPT_encrypt_blocks();
 * ECRYPT_encrypt_blocks();
 * ECRYPT_encrypt_bytes();
 *
 * ECRYPT_ivsetup();
 * ECRYPT_encrypt_blocks();
 * ECRYPT_encrypt_blocks();
 *
 * ECRYPT_ivsetup();
 * ECRYPT_encrypt_bytes();
 *
 * The following sequence is not:
 *
 * ECRYPT_keysetup();
 * ECRYPT_ivsetup();
 * ECRYPT_encrypt_blocks();
 * ECRYPT_encrypt_bytes();
 * ECRYPT_encrypt_blocks();
 */

/*
 * By default ECRYPT_encrypt_bytes() and ECRYPT_decrypt_bytes() are
 * defined as macros which redirect the call to a single function
 * ECRYPT_process_bytes(). If you want to provide separate encryption
 * and decryption functions, please undef
 * ECRYPT_HAS_SINGLE_BYTE_FUNCTION.
 */
#define ABC_HAS_SINGLE_BYTE_FUNCTION /* [edit] */
#ifdef ABC_HAS_SINGLE_BYTE_FUNCTION

    void ECRYPT_encrypt_bytes(const u8* plaintext, u8* ciphertext, u32 msglen) override;

    void ECRYPT_decrypt_bytes(const u8* ciphertext, u8* plaintext, u32 msglen) override;

    void ABC_process_bytes(int action, /* 0 = encrypt; 1 = decrypt; */
                           void* ctx,
                           const u8* input,
                           u8* output,
                           u32 msglen); /* Message length in bytes. */

#else

    void ECRYPT_encrypt_bytes(void* ctx,
                              const u8* plaintext,
                              u8* ciphertext,
                              u32 msglen); /* Message length in bytes. */

    void ECRYPT_decrypt_bytes(void* ctx,
                              const u8* ciphertext,
                              u8* plaintext,
                              u32 msglen); /* Message length in bytes. */

#endif

/* ------------------------------------------------------------------------- */

/* Optional features */

/*
 * For testing purposes it can sometimes be useful to have a function
 * which immediately generates keystream without having to provide it
 * with a zero plaintext. If your cipher cannot provide this function
 * (e.g., because it is not strictly a synchronous cipher), please
 * reset the ABC_GENERATES_KEYSTREAM flag.
 */

#define ABC_GENERATES_KEYSTREAM
#ifdef ABC_GENERATES_KEYSTREAM

    void ABC_keystream_bytes(ABC_ctx* ctx,
                             u8* keystream,
                             u32 length); /* Length of keystream in bytes. */

#endif

/* ------------------------------------------------------------------------- */

/* Optional optimizations */

/*
 * By default, the functions in this section are implemented using
 * calls to functions declared above. However, you might want to
 * implement them differently for performance reasons.
 */

/*
 * All-in-one encryption/decryption of (short) packets.
 *
 * The default definitions of these functions can be found in
 * "ecrypt-sync.c". If you want to implement them differently, please
 * undef the ABC_USES_DEFAULT_ALL_IN_ONE flag.
 */
#define ABC_USES_DEFAULT_ALL_IN_ONE /* [edit] */

/*
 * Undef ABC_HAS_SINGLE_PACKET_FUNCTION if you want to provide
 * separate packet encryption and decryption functions.
 */
#define ABC_HAS_SINGLE_PACKET_FUNCTION /* [edit] */
#ifdef ABC_HAS_SINGLE_PACKET_FUNCTION

#define ABC_encrypt_packet(ctx, iv, plaintext, ciphertext, mglen)                                  \
    ABC_process_packet(0, ctx, iv, plaintext, ciphertext, mglen)

#define ABC_decrypt_packet(ctx, iv, ciphertext, plaintext, mglen)                                  \
    ABC_process_packet(1, ctx, iv, ciphertext, plaintext, mglen)

    void ABC_process_packet(int action, /* 0 = encrypt; 1 = decrypt; */
                            ABC_ctx* ctx,
                            const u8* iv,
                            const u8* input,
                            u8* output,
                            u32 msglen);

#else

    void
    ABC_encrypt_packet(ABC_ctx* ctx, const u8* iv, const u8* plaintext, u8* ciphertext, u32 msglen);

    void
    ABC_decrypt_packet(ABC_ctx* ctx, const u8* iv, const u8* ciphertext, u8* plaintext, u32 msglen);

#endif

/*
 * Encryption/decryption of blocks.
 *
 * By default, these functions are defined as macros. If you want to
 * provide a different implementation, please undef the
 * ABC_USES_DEFAULT_BLOCK_MACROS flag and implement the functions
 * declared below.
 */

#define ABC_BLOCKLENGTH 64 /* [edit] */

    /* #define ABC_USES_DEFAULT_BLOCK_MACROS */ /* [edit] */
#ifdef ABC_USES_DEFAULT_BLOCK_MACROS

#define ABC_encrypt_blocks(ctx, plaintext, ciphertext, blocks)                                     \
    ABC_encrypt_bytes(ctx, plaintext, ciphertext, (blocks)*ABC_BLOCKLENGTH)

#define ABC_decrypt_blocks(ctx, ciphertext, plaintext, blocks)                                     \
    ABC_decrypt_bytes(ctx, ciphertext, plaintext, (blocks)*ABC_BLOCKLENGTH)

#ifdef ABC_GENERATES_KEYSTREAM

#define ABC_keystream_blocks(ctx, keystream, blocks)                                               \
    ABC_keystream_bytes(ctx, keystream, (blocks)*ABC_BLOCKLENGTH)

#endif

#else

/*
 * Undef ABC_HAS_SINGLE_BLOCK_FUNCTION if you want to provide
 * separate block encryption and decryption functions.
 */
#define ABC_HAS_SINGLE_BLOCK_FUNCTION /* [edit] */
#ifdef ABC_HAS_SINGLE_BLOCK_FUNCTION

#define ABC_encrypt_blocks(ctx, plaintext, ciphertext, blocks)                                     \
    ABC_process_blocks(0, ctx, plaintext, ciphertext, blocks)

#define ABC_decrypt_blocks(ctx, ciphertext, plaintext, blocks)                                     \
    ABC_process_blocks(1, ctx, ciphertext, plaintext, blocks)

    void ABC_process_blocks(int action, /* 0 = encrypt; 1 = decrypt; */
                            ABC_ctx* ctx,
                            const u8* input,
                            u8* output,
                            u32 blocks); /* Message length in blocks. */

#else

    void ABC_encrypt_blocks(ABC_ctx* ctx,
                            const u8* plaintext,
                            u8* ciphertext,
                            u32 blocks); /* Message length in blocks. */

    void ABC_decrypt_blocks(ABC_ctx* ctx,
                            const u8* ciphertext,
                            u8* plaintext,
                            u32 blocks); /* Message length in blocks. */

#endif

#ifdef ABC_GENERATES_KEYSTREAM

    void
    ABC_keystream_blocks(ABC_ctx* ctx, u8* keystream, u32 blocks); /* Keystream length in blocks. */

#endif

#endif
};
/*
 * If your cipher can be implemented in different ways, you can use
 * the ABC_VARIANT parameter to allow the user to choose between
 * them at compile time (e.g., gcc -DECRYPT_VARIANT=3 ...). Please
 * only use this possibility if you really think it could make a
 * significant difference and keep the number of variants
 * (ABC_MAXVARIANT) as small as possible (definitely not more than
 * 10). Note also that all variants should have exactly the same
 * external interface (i.e., the same ABC_BLOCKLENGTH, etc.).
 */
#define ABC_MAXVARIANT 10 /* [edit] */

#ifndef ABC_VARIANT
#define ABC_VARIANT 1
#endif

#if (ABC_VARIANT > ABC_MAXVARIANT)
#error this variant does not exist
#endif

} // namespace estream
} // namespace stream_ciphers
/* ------------------------------------------------------------------------- */

#endif
