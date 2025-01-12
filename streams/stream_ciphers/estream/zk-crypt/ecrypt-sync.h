/***************************************************
MODIFIED FOR ZK-Crypt
ver: 2.00 (12 feb 2006)

Submited by: Carmi Gressel et al     (carmi@fortressgb.com)
                FortressGB

Response to Ecrypt call for eSTREAM Profile II (HW)

Code IS NOT OPTIMIZED for speed
***************************************************/

/* ecrypt-sync.h */

/*
 * Header file for synchronous stream ciphers without authentication
 * mechanism.
 *
 * *** Please only edit parts marked with "[edit]". ***
 */

#ifndef ZKCRYPT_SYNC
#define ZKCRYPT_SYNC

#include "../../stream_interface.h"
#include "../ecrypt-portable.h"

namespace stream_ciphers {
namespace estream {

/* ------------------------------------------------------------------------- */

/* Cipher parameters */

/*
 * The name of your cipher.
 */
#define ZKCRYPT_NAME "ZK-Crypt-v3" /* [edit] */
#define ZKCRYPT_PROFILE "_____"

/*
 * Specify which key and IV sizes are supported by your cipher. A user
 * should be able to enumerate the supported sizes by running the
 * following code:
 *
 * for (i = 0; ZKCRYPT_KEYSIZE(i) <= ZKCRYPT_MAXKEYSIZE; ++i)
 *   {
 *     keysize = ZKCRYPT_KEYSIZE(i);
 *
 *     ...
 *   }
 *
 * All sizes are in bits.
 */

/* ZK-Crypt

       one key size (128 bit)
       one IV size (128 bit)

*/

#define ZKCRYPT_MAXKEYSIZE 160            /* [edit] */
#define ZKCRYPT_KEYSIZE(i) (128 + (i)*32) /* [edit] */

#define ZKCRYPT_MAXIVSIZE 128            /* [edit] */
#define ZKCRYPT_IVSIZE(i) (128 + (i)*32) /* [edit] */

/* ------------------------------------------------------------------------- */

/* Data structures */

/*
 * ZKCRYPT_ctx is the structure containing the representation of the
 * internal state of your cipher.
 */

typedef struct {
    /* Set up variables */
    u32 upCphWrd_1, upCphWrd_2, upCphWrd_3, upCphWrd_4, upCphWrd_5;
    u32 upIV_1, upIV_2, upIV_3, upIV_4, upIV_5;
    u8 keysize;
    u8 ivsize;

    /* State variables */
    u32 sttSuper;
    u32 sttTopBank, sttMidBank, sttBotBank;
    u32 sttFeedBack;
    u32 sttST_FeedBack;
    u32 fbA, fbB, fbC, fbD;
    u32 lclMACstorage;
    u32 sttTopXorNStore, sttIntrXorNStore, sttBotXorNStore;
    u32 sttTopHash, sttBotHash;
    u32 sttClockFeedBack, sttClockFeedBack_next;

    /* controll variables */
    u8 ctrlSuperBank;
    u8 ctrlTopBank, ctrlMidBank, ctrlBotBank;
    u8 ctrlFeedBack;
    u8 ctrlTopHashMatrix, ctrlBotHashMatrix;
    u8 ctrlClocks;

    /* clocks	*/
    u8 topBankClock_nLFR;   /* 3 bits */
    u8 topBankClockCounter; /* 4 bits */
    u8 topBankClock_MC;     /* 1 bit */

    u8 midBankClock_nLFR;   /* 5 bits */
    u8 midBankClockCounter; /* 4 bits */
    u8 midBankClock_MC;     /* 1 bit */

    u8 botBankClock_nLFR;   /* 6 bits */
    u8 botBankClockCounter; /* 4 bits */
    u8 botBankClock_MC;     /* 1 bit */

    u8 hashCounter; /* 2 bits */

    u16 longPClock; /* 9 bits */
    u8 shortPClock; /* 2 bits */

    u16 delayedBuffer; /* 9 bit  */

    /* DEBUG */
    u32 sttTest;
    u32 clockTest;

} ZKCRYPT_ctx;

/* ------------------------------------------------------------------------- */
class ECRYPT_Zkcrypt : public estream_interface {
    ZKCRYPT_ctx _ctx;

public:
    /* Mandatory functions */

    /*
     * Key and message independent initialization. This function will be
     * called once when the program starts (e.g., to build expanded S-box
     * tables).
     */
    void ECRYPT_init(void) override;

    /* [edit] */
    /*
     * Init should realy be of the following form to allow to keep changes
     * in the structure of the encryption machine
     *
     */
    void ECRYPT_init_X(void* ctx, u32 options);

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
 * By default ZKCRYPT_encrypt_bytes() and ZKCRYPT_decrypt_bytes() are
 * defined as macros which redirect the call to a single function
 * ZKCRYPT_process_bytes(). If you want to provide separate encryption
 * and decryption functions, please undef
 * ZKCRYPT_HAS_SINGLE_BYTE_FUNCTION.
 */
#define ZKCRYPT_HAS_SINGLE_BYTE_FUNCTION /* [edit] */
#ifdef ZKCRYPT_HAS_SINGLE_BYTE_FUNCTION

    void ECRYPT_encrypt_bytes(const u8* plaintext, u8* ciphertext, u32 msglen) override;

    void ECRYPT_decrypt_bytes(const u8* ciphertext, u8* plaintext, u32 msglen) override;

    void ZKCRYPT_process_bytes(int action, /* 0 = encrypt; 1 = decrypt; */
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
 * reset the ZKCRYPT_GENERATES_KEYSTREAM flag.
 */

#define ZKCRYPT_GENERATES_KEYSTREAM
#ifdef ZKCRYPT_GENERATES_KEYSTREAM

    void ZKCRYPT_keystream_bytes(ZKCRYPT_ctx* ctx,
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
 * undef the ZKCRYPT_USES_DEFAULT_ALL_IN_ONE flag.
 */
#define ZKCRYPT_USES_DEFAULT_ALL_IN_ONE /* [edit] */

/*
 * Undef ZKCRYPT_HAS_SINGLE_PACKET_FUNCTION if you want to provide
 * separate packet encryption and decryption functions.
 */
#define ZKCRYPT_HAS_SINGLE_PACKET_FUNCTION /* [edit] */
#ifdef ZKCRYPT_HAS_SINGLE_PACKET_FUNCTION

#define ZKCRYPT_encrypt_packet(ctx, iv, plaintext, ciphertext, mglen)                              \
    ZKCRYPT_process_packet(0, ctx, iv, plaintext, ciphertext, mglen)

#define ZKCRYPT_decrypt_packet(ctx, iv, ciphertext, plaintext, mglen)                              \
    ZKCRYPT_process_packet(1, ctx, iv, ciphertext, plaintext, mglen)

    void ZKCRYPT_process_packet(int action, /* 0 = encrypt; 1 = decrypt; */
                                ZKCRYPT_ctx* ctx,
                                const u8* iv,
                                const u8* input,
                                u8* output,
                                u32 msglen);

#else

    void ZKCRYPT_encrypt_packet(
            ZKCRYPT_ctx* ctx, const u8* iv, const u8* plaintext, u8* ciphertext, u32 msglen);

    void ZKCRYPT_decrypt_packet(
            ZKCRYPT_ctx* ctx, const u8* iv, const u8* ciphertext, u8* plaintext, u32 msglen);

#endif

/*
 * Encryption/decryption of blocks.
 *
 * By default, these functions are defined as macros. If you want to
 * provide a different implementation, please undef the
 * ZKCRYPT_USES_DEFAULT_BLOCK_MACROS flag and implement the functions
 * declared below.
 */

#define ZKCRYPT_BLOCKLENGTH 4 /* [edit] */

#define ZKCRYPT_USES_DEFAULT_BLOCK_MACROS /* [edit] */
#ifdef ZKCRYPT_USES_DEFAULT_BLOCK_MACROS

#define ZKCRYPT_encrypt_blocks(ctx, plaintext, ciphertext, blocks)                                 \
    ECRYPT_encrypt_bytes(ctx, plaintext, ciphertext, (blocks)*ZKCRYPT_BLOCKLENGTH)

#define ZKCRYPT_decrypt_blocks(ctx, ciphertext, plaintext, blocks)                                 \
    ECRYPT_decrypt_bytes(ctx, ciphertext, plaintext, (blocks)*ZKCRYPT_BLOCKLENGTH)

#ifdef ZKCRYPT_GENERATES_KEYSTREAM

#define ZKCRYPT_keystream_blocks(ctx, keystream, blocks)                                           \
    ZKCRYPT_keystream_bytes(ctx, keystream, (blocks)*ZKCRYPT_BLOCKLENGTH)

#endif

#else

/*
 * Undef ZKCRYPT_HAS_SINGLE_BLOCK_FUNCTION if you want to provide
 * separate block encryption and decryption functions.
 */
#define ZKCRYPT_HAS_SINGLE_BLOCK_FUNCTION /* [edit] */
#ifdef ZKCRYPT_HAS_SINGLE_BLOCK_FUNCTION

#define ZKCRYPT_encrypt_blocks(ctx, plaintext, ciphertext, blocks)                                 \
    ZKCRYPT_process_blocks(0, ctx, plaintext, ciphertext, blocks)

#define ZKCRYPT_decrypt_blocks(ctx, ciphertext, plaintext, blocks)                                 \
    ZKCRYPT_process_blocks(1, ctx, ciphertext, plaintext, blocks)

    void ZKCRYPT_process_blocks(int action, /* 0 = encrypt; 1 = decrypt; */
                                ZKCRYPT_ctx* ctx,
                                const u8* input,
                                u8* output,
                                u32 blocks); /* Message length in blocks. */

#else

    void ZKCRYPT_encrypt_blocks(ZKCRYPT_ctx* ctx,
                                const u8* plaintext,
                                u8* ciphertext,
                                u32 blocks); /* Message length in blocks. */

    void ZKCRYPT_decrypt_blocks(ZKCRYPT_ctx* ctx,
                                const u8* ciphertext,
                                u8* plaintext,
                                u32 blocks); /* Message length in blocks. */

#endif

#ifdef ZKCRYPT_GENERATES_KEYSTREAM

    void ZKCRYPT_keystream_blocks(ZKCRYPT_ctx* ctx,
                                  u8* keystream,
                                  u32 blocks); /* Keystream length in blocks. */

#endif

#endif
};
/*
 * If your cipher can be implemented in different ways, you can use
 * the ZKCRYPT_VARIANT parameter to allow the user to choose between
 * them at compile time (e.g., gcc -DECRYPT_VARIANT=3 ...). Please
 * only use this possibility if you really think it could make a
 * significant difference and keep the number of variants
 * (ZKCRYPT_MAXVARIANT) as small as possible (definitely not more than
 * 10). Note also that all variants should have exactly the same
 * external interface (i.e., the same ZKCRYPT_BLOCKLENGTH, etc.).
 */
#define ZKCRYPT_MAXVARIANT 1 /* [edit] */

#ifndef ZKCRYPT_VARIANT
#define ZKCRYPT_VARIANT 1
#endif

#if (ZKCRYPT_VARIANT > ZKCRYPT_MAXVARIANT)
#error this variant does not exist
#endif

} // namespace estream
} // namespace stream_ciphers

/* ------------------------------------------------------------------------- */

#endif
