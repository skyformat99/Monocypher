#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "monocypher.h"
#include "sha512.h"

#define FOR(i, start, end) for (size_t (i) = (start); (i) < (end); (i)++)
typedef uint8_t   u8;
typedef uint32_t u32;
typedef  int32_t i32;
typedef  int64_t i64;
typedef uint64_t u64;

// Deterministic "random" number generator, so we can make "random", yet
// reproducible tests.  To change the random stream, change the seed.
void p_random(u8 *stream, size_t size)
{
    static crypto_chacha_ctx ctx;
    static int is_init = 0;
    if (!is_init) {
        static const u8 seed[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
        crypto_chacha20_init(&ctx, seed, seed);
        is_init = 1;
    }
    crypto_chacha20_stream(&ctx, stream, size);
}

// Random 64 bit number
u64 rand64()
{
    u8  tmp;
    u64 result = 0;
    FOR (i, 0, 8) {
        p_random(&tmp, 1);
        result <<= 8;
        result  += tmp;
    }
    return result;
}

// Tests that encrypting in chunks yields the same result than
// encrypting all at once.
static int chacha20()
{
    static const size_t block_size = 64;             // Chacha Block size
    static const size_t input_size = block_size * 4; // total input size
    static const size_t c_max_size = block_size * 2; // maximum chunk size
    int status = 0;
    FOR (i, 0, 1000) {
        size_t offset = 0;
        // outputs
        u8 output_chunk[input_size];
        u8 output_whole[input_size];
        // inputs
        u8 input       [input_size];  p_random(input, input_size);
        u8 key         [32];          p_random(key  , 32);
        u8 nonce       [8];           p_random(nonce, 8);

        // Encrypt in chunks
        crypto_chacha_ctx ctx;
        crypto_chacha20_init(&ctx, key, nonce);
        while (1) {
            size_t chunk_size = rand64() % c_max_size;
            if (offset + chunk_size > input_size) { break; }
            u8 *out = output_chunk + offset;
            u8 *in  = input        + offset;
            crypto_chacha20_encrypt(&ctx, out, in, chunk_size);
            offset += chunk_size;
        }
        // Encrypt all at once
        crypto_chacha20_init(&ctx, key, nonce);
        crypto_chacha20_encrypt(&ctx, output_whole, input, offset);

        // Compare the results (must be the same)
        status |= crypto_memcmp(output_chunk, output_whole, offset);
    }
    printf("%s: Chacha20\n", status != 0 ? "FAILED" : "OK");
    return status;
}

static int chacha20_set_ctr()
{
    static const size_t nb_blocks   = 10;
    static const size_t block_size  = 64;
    static const size_t stream_size = block_size * nb_blocks;
    int status = 0;
    FOR (i, 0, 1000) {
        u8 output_part[stream_size    ];
        u8 output_all [stream_size    ];
        u8 output_more[stream_size * 2];
        u8 key        [32];          p_random(key  , 32);
        u8 nonce      [8];           p_random(nonce, 8 );
        size_t ctr   = rand64() % nb_blocks;
        size_t limit = ctr * block_size;
        // Encrypt all at once
        crypto_chacha_ctx ctx;
        crypto_chacha20_init(&ctx, key, nonce);
        crypto_chacha20_stream(&ctx, output_all, stream_size);
        // Encrypt second part
        crypto_chacha20_set_ctr(&ctx, ctr);
        crypto_chacha20_stream(&ctx, output_part + limit, stream_size - limit);
        // Encrypt first part
        crypto_chacha20_set_ctr(&ctx, 0);
        crypto_chacha20_stream(&ctx, output_part, limit);
        // Compare the results (must be the same)
        status |= crypto_memcmp(output_part, output_all, stream_size);

        // Encrypt before the begining
        crypto_chacha20_set_ctr(&ctx, -ctr);
        crypto_chacha20_stream(&ctx,
                               output_more + stream_size - limit,
                               stream_size + limit);
        // Compare the results (must be the same)
        status |= crypto_memcmp(output_more + stream_size,
                                output_all,
                                stream_size);
    }
    printf("%s: Chacha20 (set counter)\n", status != 0 ? "FAILED" : "OK");
    return status;
}

// Tests that authenticating bit by bit yields the same mac than
// authenticating all at once
static int poly1305()
{
    static const size_t block_size = 16;             // poly1305 block size
    static const size_t input_size = block_size * 4; // total input size
    static const size_t c_max_size = block_size * 2; // maximum chunk size
    int status = 0;
    FOR (i, 0, 1000) {
        size_t offset = 0;
        // outputs
        u8 mac_chunk[16];
        u8 mac_whole[16];
        // inputs
        u8 input[input_size];  p_random(input, input_size);
        u8 key  [32];          p_random(key  , 32);

        // Authenticate bit by bit
        crypto_poly1305_ctx ctx;
        crypto_poly1305_init(&ctx, key);
        while (1) {
            size_t chunk_size = rand64() % c_max_size;
            if (offset + chunk_size > input_size) { break; }
            crypto_poly1305_update(&ctx, input + offset, chunk_size);
            offset += chunk_size;
        }
        crypto_poly1305_final(&ctx, mac_chunk);

        // Authenticate all at once
        crypto_poly1305_auth(mac_whole, input, offset, key);

        // Compare the results (must be the same)
        status |= crypto_memcmp(mac_chunk, mac_whole, 16);
    }
    printf("%s: Poly1305\n", status != 0 ? "FAILED" : "OK");
    return status;
}

// Tests that hashing bit by bit yields the same hash than hashing all
// at once.  Note: I figured we didn't need to test keyed mode, or
// different hash sizes, again.  This test sticks to the simplified
// interface.
static int blake2b()
{
    static const size_t block_size = 128;            // Blake2b block size
    static const size_t input_size = block_size * 4; // total input size
    static const size_t c_max_size = block_size * 2; // maximum chunk size
    int status = 0;
    FOR (i, 0, 1000) {
        size_t offset = 0;
        // outputs
        u8 hash_chunk[64];
        u8 hash_whole[64];
        // inputs
        u8 input[input_size];  p_random(input, input_size);

        // Authenticate bit by bit
        crypto_blake2b_ctx ctx;
        crypto_blake2b_init(&ctx);
        while (1) {
            size_t chunk_size = rand64() % c_max_size;
            if (offset + chunk_size > input_size) { break; }
            crypto_blake2b_update(&ctx, input + offset, chunk_size);
            offset += chunk_size;
        }
        crypto_blake2b_final(&ctx, hash_chunk);

        // Authenticate all at once
        crypto_blake2b(hash_whole, input, offset);

        // Compare the results (must be the same)
        status |= crypto_memcmp(hash_chunk, hash_whole, 64);
    }
    printf("%s: Blake2b\n", status != 0 ? "FAILED" : "OK");
    return status;
}

static int aead()
{
    int status = 0;
    FOR (i, 0, 1000) {
        u8 key      [32];  p_random(key      , 32);
        u8 nonce    [24];  p_random(nonce    , 24);
        u8 ad       [ 4];  p_random(ad       ,  4);
        u8 plaintext[ 8];  p_random(plaintext,  8);
        u8 box[24], box2[24];
        u8 out[8];
        // AEAD roundtrip
        crypto_aead_lock(box, box+16, key, nonce, ad, 4, plaintext, 8);
        status |= crypto_aead_unlock(out, key, nonce, box, ad, 4, box+16, 8);
        status |= crypto_memcmp(plaintext, out, 8);
        box[0]++;
        status |= !crypto_aead_unlock(out, key, nonce, box, ad, 4, box+16, 8);

        // Authenticated roundtrip (easy interface)
        // Make and accept message
        crypto_lock(box, box + 16, key, nonce, plaintext, 8);
        status |= crypto_unlock(out, key, nonce, box, box + 16, 8);
        // Make sure decrypted text and original text are the same
        status |= crypto_memcmp(plaintext, out, 8);
        // Make and reject forgery
        box[0]++;
        status |= !crypto_unlock(out, key, nonce, box, box + 16, 8);
        box[0]--; // undo forgery

        // Same result for both interfaces
        crypto_aead_lock(box2, box2 + 16, key, nonce, 0, 0, plaintext, 8);
        status |= crypto_memcmp(box, box2, 24);
    }
    printf("%s: aead\n", status != 0 ? "FAILED" : "OK");
    return status;
}

int main(void)
{
    int status = 0;
    status |= chacha20();
    status |= chacha20_set_ctr();
    status |= poly1305();
    status |= blake2b();
    status |= aead();
    return status;
}
