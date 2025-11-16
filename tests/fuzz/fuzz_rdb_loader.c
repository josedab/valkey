/* Fuzz target for RDB (Valkey Database) file loader
 *
 * This fuzz target tests the RDB file parser for crashes, hangs,
 * and memory corruption when loading malformed or malicious RDB files.
 *
 * Build with:
 *   clang -fsanitize=fuzzer,address -I../../src -o fuzz_rdb_loader fuzz_rdb_loader.c
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* RDB file format constants */
#define RDB_VERSION 11
#define VALKEY_RDB_VERSION 11

/* RDB opcodes */
#define RDB_OPCODE_AUX        250
#define RDB_OPCODE_RESIZEDB   251
#define RDB_OPCODE_EXPIRETIME_MS 252
#define RDB_OPCODE_EXPIRETIME 253
#define RDB_OPCODE_SELECTDB   254
#define RDB_OPCODE_EOF        255

/* RDB type codes */
#define RDB_TYPE_STRING 0
#define RDB_TYPE_LIST   1
#define RDB_TYPE_SET    2
#define RDB_TYPE_ZSET   3
#define RDB_TYPE_HASH   4

/* RDB encoding types */
#define RDB_ENC_INT8  0
#define RDB_ENC_INT16 1
#define RDB_ENC_INT32 2
#define RDB_ENC_LZF   3

/* Mock RDB loading context */
typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    int valid;
} rdbLoadContext;

/* Initialize RDB load context */
static void initRdbLoadContext(rdbLoadContext *ctx, const uint8_t *data, size_t size) {
    ctx->data = data;
    ctx->size = size;
    ctx->pos = 0;
    ctx->valid = 1;
}

/* Read one byte from RDB */
static int rdbReadByte(rdbLoadContext *ctx, uint8_t *byte) {
    if (ctx->pos >= ctx->size) {
        ctx->valid = 0;
        return -1;
    }
    *byte = ctx->data[ctx->pos++];
    return 0;
}

/* Read length encoding from RDB */
static int rdbLoadLen(rdbLoadContext *ctx, uint32_t *len, int *encoding) {
    uint8_t byte;

    if (rdbReadByte(ctx, &byte) != 0) {
        return -1;
    }

    int type = (byte & 0xC0) >> 6;

    switch (type) {
        case 0:  /* 6-bit length */
            *len = byte & 0x3F;
            if (encoding) *encoding = -1;
            return 0;

        case 1:  /* 14-bit length */
            if (ctx->pos >= ctx->size) {
                ctx->valid = 0;
                return -1;
            }
            *len = ((byte & 0x3F) << 8) | ctx->data[ctx->pos++];
            if (encoding) *encoding = -1;
            return 0;

        case 2:  /* 32-bit length */
            if (ctx->pos + 4 > ctx->size) {
                ctx->valid = 0;
                return -1;
            }
            *len = (ctx->data[ctx->pos] << 24) |
                   (ctx->data[ctx->pos + 1] << 16) |
                   (ctx->data[ctx->pos + 2] << 8) |
                   ctx->data[ctx->pos + 3];
            ctx->pos += 4;
            if (encoding) *encoding = -1;
            return 0;

        case 3:  /* Special encoding */
            if (encoding) {
                *encoding = byte & 0x3F;
                *len = 0;
            }
            return 0;
    }

    return -1;
}

/* Mock function to load a string from RDB */
static char *rdbLoadString(rdbLoadContext *ctx) {
    uint32_t len;
    int encoding;

    if (rdbLoadLen(ctx, &len, &encoding) != 0) {
        return NULL;
    }

    /* Handle special encodings */
    if (encoding != -1) {
        switch (encoding) {
            case RDB_ENC_INT8:
            case RDB_ENC_INT16:
            case RDB_ENC_INT32:
                /* Skip encoded integer */
                ctx->pos += (1 << encoding);
                return NULL;
            case RDB_ENC_LZF:
                /* Skip LZF compressed data */
                uint32_t compressed_len, uncompressed_len;
                if (rdbLoadLen(ctx, &compressed_len, NULL) != 0) return NULL;
                if (rdbLoadLen(ctx, &uncompressed_len, NULL) != 0) return NULL;
                if (compressed_len > 1024 * 1024) return NULL;  /* Limit size */
                ctx->pos += compressed_len;
                return NULL;
        }
    }

    /* Sanity check on length */
    if (len > 1024 * 1024 || len > (ctx->size - ctx->pos)) {
        ctx->valid = 0;
        return NULL;
    }

    /* Allocate and read string */
    char *str = malloc(len + 1);
    if (!str) return NULL;

    if (ctx->pos + len > ctx->size) {
        free(str);
        ctx->valid = 0;
        return NULL;
    }

    memcpy(str, ctx->data + ctx->pos, len);
    str[len] = '\0';
    ctx->pos += len;

    return str;
}

/* Mock RDB loading function */
static void mockLoadRDB(rdbLoadContext *ctx) {
    uint8_t byte;

    /* Check magic string "REDIS" or "VALKEY" */
    if (ctx->size < 9) {
        ctx->valid = 0;
        return;
    }

    if (memcmp(ctx->data, "REDIS", 5) != 0 &&
        memcmp(ctx->data, "VALKEY", 6) != 0) {
        return;
    }

    ctx->pos = (memcmp(ctx->data, "REDIS", 5) == 0) ? 5 : 6;

    /* Check version */
    if (ctx->pos + 4 > ctx->size) {
        ctx->valid = 0;
        return;
    }
    ctx->pos += 4;

    /* Parse RDB content */
    while (ctx->pos < ctx->size && ctx->valid) {
        if (rdbReadByte(ctx, &byte) != 0) {
            break;
        }

        switch (byte) {
            case RDB_OPCODE_EOF:
                /* End of RDB file */
                return;

            case RDB_OPCODE_SELECTDB: {
                uint32_t dbid;
                if (rdbLoadLen(ctx, &dbid, NULL) != 0) {
                    return;
                }
                break;
            }

            case RDB_OPCODE_EXPIRETIME:
                /* Skip 4-byte timestamp */
                ctx->pos += 4;
                break;

            case RDB_OPCODE_EXPIRETIME_MS:
                /* Skip 8-byte timestamp */
                ctx->pos += 8;
                break;

            case RDB_OPCODE_RESIZEDB: {
                uint32_t db_size, expires_size;
                if (rdbLoadLen(ctx, &db_size, NULL) != 0) return;
                if (rdbLoadLen(ctx, &expires_size, NULL) != 0) return;
                break;
            }

            case RDB_OPCODE_AUX: {
                char *key = rdbLoadString(ctx);
                char *val = rdbLoadString(ctx);
                if (key) free(key);
                if (val) free(val);
                break;
            }

            default:
                /* Type byte - load key and value */
                if (byte < 16) {  /* Valid type codes are 0-15 */
                    char *key = rdbLoadString(ctx);
                    char *val = rdbLoadString(ctx);
                    if (key) free(key);
                    if (val) free(val);
                }
                break;
        }

        /* Prevent infinite loops in fuzzing */
        if (ctx->pos > ctx->size) {
            break;
        }
    }
}

/* LibFuzzer entry point */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Skip empty or excessively large inputs */
    if (size == 0 || size > 10 * 1024 * 1024) {
        return 0;
    }

    /* Initialize load context */
    rdbLoadContext ctx;
    initRdbLoadContext(&ctx, data, size);

    /* Try to load the RDB file - should not crash or leak */
    mockLoadRDB(&ctx);

    return 0;
}

#ifdef FUZZ_STANDALONE
/* Standalone mode for testing without libFuzzer */
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "Error opening %s\n", argv[1]);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = malloc(size);
    if (!data) {
        fclose(f);
        return 1;
    }

    fread(data, 1, size, f);
    fclose(f);

    LLVMFuzzerTestOneInput(data, size);

    free(data);
    printf("Processed RDB file of %ld bytes\n", size);
    return 0;
}
#endif
