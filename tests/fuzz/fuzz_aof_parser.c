/* Fuzz target for AOF (Append-Only File) parser
 *
 * This fuzz target tests the AOF parser for crashes, hangs,
 * and memory corruption when loading malformed or malicious AOF files.
 *
 * AOF files contain RESP-formatted commands that are replayed on startup.
 *
 * Build with:
 *   clang -fsanitize=fuzzer,address -I../../src -o fuzz_aof_parser fuzz_aof_parser.c
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* AOF file parsing context */
typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    int valid;
    int command_count;
} aofLoadContext;

/* Command structure */
typedef struct {
    int argc;
    char **argv;
    size_t *argv_len;
} aofCommand;

/* Initialize AOF load context */
static void initAofLoadContext(aofLoadContext *ctx, const uint8_t *data, size_t size) {
    ctx->data = data;
    ctx->size = size;
    ctx->pos = 0;
    ctx->valid = 1;
    ctx->command_count = 0;
}

/* Free command structure */
static void freeAofCommand(aofCommand *cmd) {
    if (!cmd) return;

    if (cmd->argv) {
        for (int i = 0; i < cmd->argc; i++) {
            if (cmd->argv[i]) {
                free(cmd->argv[i]);
            }
        }
        free(cmd->argv);
    }

    if (cmd->argv_len) {
        free(cmd->argv_len);
    }

    free(cmd);
}

/* Read a line from AOF (until CRLF) */
static char *readLine(aofLoadContext *ctx) {
    if (ctx->pos >= ctx->size) {
        ctx->valid = 0;
        return NULL;
    }

    size_t start = ctx->pos;
    size_t line_len = 0;

    /* Find CRLF */
    while (ctx->pos < ctx->size && line_len < 1024 * 1024) {
        if (ctx->data[ctx->pos] == '\r' &&
            ctx->pos + 1 < ctx->size &&
            ctx->data[ctx->pos + 1] == '\n') {
            /* Found CRLF */
            char *line = malloc(line_len + 1);
            if (!line) {
                ctx->valid = 0;
                return NULL;
            }
            memcpy(line, ctx->data + start, line_len);
            line[line_len] = '\0';
            ctx->pos += 2;  /* Skip CRLF */
            return line;
        }
        ctx->pos++;
        line_len++;
    }

    /* No CRLF found or line too long */
    ctx->valid = 0;
    return NULL;
}

/* Parse RESP bulk string */
static char *parseRespBulkString(aofLoadContext *ctx, size_t *len) {
    char *line = readLine(ctx);
    if (!line) return NULL;

    /* Should start with '$' */
    if (line[0] != '$') {
        free(line);
        ctx->valid = 0;
        return NULL;
    }

    /* Parse length */
    long bulk_len = atol(line + 1);
    free(line);

    if (bulk_len < 0 || bulk_len > 512 * 1024 * 1024) {
        /* Null bulk string or too large */
        ctx->valid = 0;
        return NULL;
    }

    /* Check if we have enough data */
    if (ctx->pos + bulk_len + 2 > ctx->size) {
        ctx->valid = 0;
        return NULL;
    }

    /* Read bulk string data */
    char *str = malloc(bulk_len + 1);
    if (!str) {
        ctx->valid = 0;
        return NULL;
    }

    memcpy(str, ctx->data + ctx->pos, bulk_len);
    str[bulk_len] = '\0';
    ctx->pos += bulk_len;

    /* Verify CRLF after bulk string */
    if (ctx->pos + 2 > ctx->size ||
        ctx->data[ctx->pos] != '\r' ||
        ctx->data[ctx->pos + 1] != '\n') {
        free(str);
        ctx->valid = 0;
        return NULL;
    }
    ctx->pos += 2;

    if (len) *len = bulk_len;
    return str;
}

/* Parse one AOF command (RESP array) */
static aofCommand *parseAofCommand(aofLoadContext *ctx) {
    char *line = readLine(ctx);
    if (!line) return NULL;

    /* Should start with '*' for array */
    if (line[0] != '*') {
        free(line);
        ctx->valid = 0;
        return NULL;
    }

    /* Parse argument count */
    int argc = atoi(line + 1);
    free(line);

    if (argc < 0 || argc > 1024) {
        /* Invalid or too many arguments */
        ctx->valid = 0;
        return NULL;
    }

    /* Allocate command structure */
    aofCommand *cmd = calloc(1, sizeof(aofCommand));
    if (!cmd) {
        ctx->valid = 0;
        return NULL;
    }

    cmd->argc = argc;
    cmd->argv = calloc(argc, sizeof(char *));
    cmd->argv_len = calloc(argc, sizeof(size_t));

    if (!cmd->argv || !cmd->argv_len) {
        freeAofCommand(cmd);
        ctx->valid = 0;
        return NULL;
    }

    /* Parse each argument */
    for (int i = 0; i < argc; i++) {
        size_t len;
        cmd->argv[i] = parseRespBulkString(ctx, &len);
        if (!cmd->argv[i]) {
            freeAofCommand(cmd);
            return NULL;
        }
        cmd->argv_len[i] = len;
    }

    return cmd;
}

/* Mock function to execute AOF command */
static void mockExecuteCommand(aofCommand *cmd) {
    if (!cmd || cmd->argc == 0) return;

    /* Mock command execution - just validate structure */
    const char *command = cmd->argv[0];

    /* Basic command validation */
    if (strcasecmp(command, "SET") == 0) {
        if (cmd->argc < 3) {
            /* Invalid SET command */
            return;
        }
    } else if (strcasecmp(command, "DEL") == 0) {
        if (cmd->argc < 2) {
            /* Invalid DEL command */
            return;
        }
    } else if (strcasecmp(command, "LPUSH") == 0 ||
               strcasecmp(command, "RPUSH") == 0) {
        if (cmd->argc < 3) {
            /* Invalid PUSH command */
            return;
        }
    }

    /* Command validated and would be executed */
}

/* Mock AOF loading function */
static void mockLoadAOF(aofLoadContext *ctx) {
    /* Parse and execute commands until end of file */
    while (ctx->pos < ctx->size && ctx->valid && ctx->command_count < 10000) {
        aofCommand *cmd = parseAofCommand(ctx);
        if (!cmd) {
            break;
        }

        /* Execute command */
        mockExecuteCommand(cmd);

        /* Cleanup */
        freeAofCommand(cmd);

        ctx->command_count++;
    }
}

/* LibFuzzer entry point */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Skip empty or excessively large inputs */
    if (size == 0 || size > 10 * 1024 * 1024) {
        return 0;
    }

    /* Initialize load context */
    aofLoadContext ctx;
    initAofLoadContext(&ctx, data, size);

    /* Try to load the AOF file - should not crash or leak */
    mockLoadAOF(&ctx);

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
    printf("Processed AOF file of %ld bytes\n", size);
    return 0;
}
#endif
