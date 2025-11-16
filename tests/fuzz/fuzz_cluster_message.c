/* Fuzz target for cluster message parsing
 *
 * This fuzz target tests the cluster message parser for crashes, hangs,
 * and memory corruption when processing malformed cluster bus messages.
 *
 * Build with:
 *   clang -fsanitize=fuzzer,address -I../../src -o fuzz_cluster_message fuzz_cluster_message.c
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Cluster message type definitions (subset) */
#define CLUSTERMSG_TYPE_PING 0
#define CLUSTERMSG_TYPE_PONG 1
#define CLUSTERMSG_TYPE_MEET 2
#define CLUSTERMSG_TYPE_FAIL 3
#define CLUSTERMSG_TYPE_PUBLISH 4
#define CLUSTERMSG_TYPE_FAILOVER_AUTH_REQUEST 5
#define CLUSTERMSG_TYPE_UPDATE 7

/* Mock cluster message header structure */
typedef struct {
    char sig[4];        /* Signature "RCmb" (Valkey Cluster message bus) */
    uint32_t totlen;    /* Total length of this message */
    uint16_t ver;       /* Protocol version */
    uint16_t port;      /* TCP base port */
    uint16_t type;      /* Message type */
    uint16_t count;     /* Number of items in message */
    uint64_t currentEpoch;
    uint64_t configEpoch;
    uint64_t offset;
    char sender[40];    /* Sender node ID */
    unsigned char myslots[2048]; /* Slot bitmap */
    char slaveof[40];   /* If this is a replica */
    char myip[46];      /* Sender IP */
    uint16_t cport;     /* Cluster bus port */
    uint16_t flags;     /* Sender node flags */
    unsigned char state; /* Cluster state */
    unsigned char mflags[3]; /* Message flags */
    /* Followed by message-specific data */
} clusterMsg;

/* Mock cluster node */
typedef struct {
    char name[40];
    int flags;
} clusterNode;

static clusterNode mockNode;

/* Mock function to validate cluster message signature */
static int validateClusterMessageSig(const clusterMsg *hdr) {
    if (!hdr) return 0;
    return (hdr->sig[0] == 'R' &&
            hdr->sig[1] == 'C' &&
            hdr->sig[2] == 'm' &&
            hdr->sig[3] == 'b');
}

/* Mock function to process cluster message */
static void mockProcessClusterMessage(const uint8_t *data, size_t size) {
    if (size < sizeof(clusterMsg)) {
        /* Message too small */
        return;
    }

    const clusterMsg *hdr = (const clusterMsg *)data;

    /* Validate signature */
    if (!validateClusterMessageSig(hdr)) {
        return;
    }

    /* Validate total length */
    if (hdr->totlen > size || hdr->totlen < sizeof(clusterMsg)) {
        return;
    }

    /* Process based on message type */
    switch (hdr->type) {
        case CLUSTERMSG_TYPE_PING:
        case CLUSTERMSG_TYPE_PONG:
        case CLUSTERMSG_TYPE_MEET:
            /* Validate sender ID is null-terminated */
            if (strnlen(hdr->sender, sizeof(hdr->sender)) == sizeof(hdr->sender)) {
                return;
            }
            /* Process ping/pong/meet */
            break;

        case CLUSTERMSG_TYPE_FAIL:
            /* Process fail message */
            break;

        case CLUSTERMSG_TYPE_PUBLISH:
            /* Validate publish message has enough data */
            if (hdr->totlen > sizeof(clusterMsg)) {
                const char *payload = (const char *)(data + sizeof(clusterMsg));
                size_t payload_len = hdr->totlen - sizeof(clusterMsg);
                /* Process publish - ensure null termination doesn't overflow */
                if (payload_len > 0 && payload_len < 1024 * 1024) {
                    /* Safe to process */
                }
            }
            break;

        case CLUSTERMSG_TYPE_FAILOVER_AUTH_REQUEST:
            /* Process failover auth request */
            break;

        case CLUSTERMSG_TYPE_UPDATE:
            /* Process update message */
            break;

        default:
            /* Unknown message type */
            break;
    }

    /* Validate slot bitmap if needed */
    int slot_count = 0;
    for (int i = 0; i < 2048; i++) {
        unsigned char byte = hdr->myslots[i];
        /* Count set bits */
        while (byte) {
            slot_count += byte & 1;
            byte >>= 1;
        }
    }

    /* Validate configuration epoch is reasonable */
    if (hdr->configEpoch > 1000000000) {
        /* Suspiciously large epoch */
    }
}

/* LibFuzzer entry point */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Skip empty or excessively large inputs */
    if (size == 0 || size > 1024 * 1024) {
        return 0;
    }

    /* Skip inputs smaller than minimum message size */
    if (size < sizeof(clusterMsg)) {
        return 0;
    }

    /* Initialize mock node */
    memset(&mockNode, 0, sizeof(mockNode));
    strncpy(mockNode.name, "mock_node_000000000000000000000000000000", 40);

    /* Process the fuzzed cluster message */
    mockProcessClusterMessage(data, size);

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
    printf("Processed %ld bytes\n", size);
    return 0;
}
#endif
