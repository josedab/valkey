/* Fuzz target for RESP (REdis Serialization Protocol) parser
 *
 * This fuzz target tests the RESP protocol parser for crashes, hangs,
 * and memory corruption issues when processing malformed or malicious input.
 *
 * Build with:
 *   clang -fsanitize=fuzzer,address -I../../src -o fuzz_resp_parser fuzz_resp_parser.c
 *
 * Or for OSS-Fuzz integration, use build_fuzz.sh
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * Minimal mock definitions for fuzzing without full server dependencies.
 * In a production fuzz setup, these would link against actual server code.
 */

typedef struct client {
    char *querybuf;
    size_t querybuf_len;
    size_t qb_pos;
    int argc;
    void **argv;
} client;

/* Mock client creation - simplified for fuzzing */
static client *createFakeClient(void) {
    client *c = calloc(1, sizeof(client));
    if (!c) return NULL;
    c->querybuf = NULL;
    c->querybuf_len = 0;
    c->qb_pos = 0;
    c->argc = 0;
    c->argv = NULL;
    return c;
}

/* Mock client cleanup */
static void freeFakeClient(client *c) {
    if (!c) return;
    if (c->querybuf) free(c->querybuf);
    if (c->argv) {
        for (int i = 0; i < c->argc; i++) {
            if (c->argv[i]) free(c->argv[i]);
        }
        free(c->argv);
    }
    free(c);
}

/*
 * Mock RESP parser - simplified version for demonstration.
 * In production, this would call the actual processInputBuffer() function.
 */
static void mockProcessInputBuffer(client *c) {
    if (!c || !c->querybuf) return;

    size_t pos = c->qb_pos;
    size_t len = c->querybuf_len;

    while (pos < len) {
        char type = c->querybuf[pos];

        switch (type) {
            case '+':  /* Simple string */
            case '-':  /* Error */
            case ':':  /* Integer */
                /* Find CRLF */
                while (pos < len && c->querybuf[pos] != '\n') pos++;
                if (pos < len) pos++;
                break;

            case '$':  /* Bulk string */
                /* Parse length */
                pos++;
                while (pos < len && c->querybuf[pos] >= '0' && c->querybuf[pos] <= '9') {
                    pos++;
                }
                /* Skip CRLF and string content */
                while (pos < len && c->querybuf[pos] != '\n') pos++;
                if (pos < len) pos++;
                break;

            case '*':  /* Array */
                /* Parse count */
                pos++;
                while (pos < len && c->querybuf[pos] >= '0' && c->querybuf[pos] <= '9') {
                    pos++;
                }
                while (pos < len && c->querybuf[pos] != '\n') pos++;
                if (pos < len) pos++;
                break;

            default:
                /* Unknown type, skip */
                pos++;
                break;
        }

        /* Prevent infinite loops */
        if (pos == c->qb_pos) {
            pos++;
        }
        c->qb_pos = pos;

        /* Limit iterations to prevent timeouts */
        if (pos > len || (pos - c->qb_pos) > 10000) {
            break;
        }
    }
}

/* LibFuzzer entry point */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Skip empty or excessively large inputs */
    if (size == 0 || size > 1024 * 1024) {
        return 0;
    }

    /* Create fake client */
    client *c = createFakeClient();
    if (!c) {
        return 0;
    }

    /* Copy fuzz input to query buffer */
    c->querybuf = malloc(size + 1);
    if (!c->querybuf) {
        freeFakeClient(c);
        return 0;
    }

    memcpy(c->querybuf, data, size);
    c->querybuf[size] = '\0';
    c->querybuf_len = size;
    c->qb_pos = 0;

    /* Try to parse input - should not crash, leak, or corrupt memory */
    mockProcessInputBuffer(c);

    /* Cleanup */
    freeFakeClient(c);

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
    return 0;
}
#endif
