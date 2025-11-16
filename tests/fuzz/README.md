# Fuzz Testing Targets

This directory contains fuzz testing targets for Valkey, as defined in RFC 0004.

## Overview

Fuzz testing (fuzzing) is an automated software testing technique that provides invalid, unexpected, or random data as input to a program. The goal is to find crashes, assertion failures, and memory corruption bugs.

## Available Fuzz Targets

### 1. `fuzz_resp_parser.c`

Tests the RESP (REdis Serialization Protocol) parser for crashes and memory issues.

**What it tests:**
- RESP protocol parsing
- Malformed command handling
- Buffer overflow vulnerabilities
- Memory corruption issues

**Input format:** Raw RESP protocol data (Simple strings, Errors, Integers, Bulk strings, Arrays)

### 2. `fuzz_cluster_message.c`

Tests cluster bus message parsing.

**What it tests:**
- Cluster message header validation
- Message type handling
- Slot bitmap processing
- Node ID validation

**Input format:** Binary cluster bus messages with "RCmb" signature

### 3. `fuzz_rdb_loader.c`

Tests RDB (Valkey/Redis Database) file loading.

**What it tests:**
- RDB file format parsing
- Version compatibility
- Opcode handling
- String/object deserialization

**Input format:** Binary RDB files starting with "REDIS" or "VALKEY" magic bytes

### 4. `fuzz_aof_parser.c`

Tests AOF (Append-Only File) parsing and command replay.

**What it tests:**
- RESP command parsing
- Command validation
- Multi-line parsing
- Edge cases in AOF format

**Input format:** RESP-formatted commands (typically multi-bulk arrays)

## Building Fuzz Targets

### Prerequisites

```bash
# Install clang (required for fuzzing instrumentation)
sudo apt-get install clang

# For OSS-Fuzz integration
# See: https://google.github.io/oss-fuzz/
```

### Build for Local Testing (Standalone)

```bash
cd tests/fuzz
./build_fuzz.sh standalone

# Output: tests/fuzz/bin/fuzz_*
```

### Build for Continuous Fuzzing (OSS-Fuzz)

```bash
cd tests/fuzz
./build_fuzz.sh ossfuzz

# Output: tests/fuzz/out/fuzz_*
```

### Manual Build

```bash
# Standalone executable
clang -g -O0 -DFUZZ_STANDALONE \
  -I../../src \
  fuzz_resp_parser.c \
  -o fuzz_resp_parser

# LibFuzzer instrumented
clang -fsanitize=fuzzer,address \
  -I../../src \
  fuzz_resp_parser.c \
  -o fuzz_resp_parser
```

## Running Fuzz Tests

### Standalone Mode

Test with a specific input file:

```bash
./bin/fuzz_resp_parser testcase.resp
```

### LibFuzzer Mode

Run continuous fuzzing:

```bash
# Basic fuzzing
./out/fuzz_resp_parser

# With corpus directory
mkdir -p corpus
./out/fuzz_resp_parser corpus/

# With options
./out/fuzz_resp_parser \
  -max_total_time=3600 \
  -timeout=10 \
  -max_len=1048576 \
  corpus/
```

### Common LibFuzzer Options

| Option | Description |
|--------|-------------|
| `-max_total_time=N` | Stop after N seconds |
| `-timeout=N` | Timeout per test input (seconds) |
| `-max_len=N` | Maximum input length |
| `-dict=file` | Use dictionary file for mutations |
| `-jobs=N` | Parallel fuzzing jobs |
| `-workers=N` | Worker processes |

## Seed Corpora

Seed corpora are included to bootstrap fuzzing with valid inputs:

```
tests/fuzz/out/
├── fuzz_resp_parser_seed_corpus/
│   ├── ping.resp       # Simple PING command
│   ├── get.resp        # GET command
│   └── set.resp        # SET command
├── fuzz_cluster_message_seed_corpus/
│   └── header.bin      # Basic cluster message
├── fuzz_rdb_loader_seed_corpus/
│   └── simple.rdb      # Minimal RDB file
└── fuzz_aof_parser_seed_corpus/
    ├── select.aof      # SELECT command
    └── set.aof         # SET command
```

## Creating Custom Test Cases

### RESP Commands

```bash
# Create a SET command
printf '*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n' > testcase.resp
./bin/fuzz_resp_parser testcase.resp
```

### Cluster Messages

```bash
# Create a minimal cluster message
printf 'RCmb' > testcase.bin
dd if=/dev/zero bs=1 count=100 >> testcase.bin
./bin/fuzz_cluster_message testcase.bin
```

### RDB Files

```bash
# Create a minimal RDB file
printf 'REDIS0011\xFE\x00\xFF' > testcase.rdb
./bin/fuzz_rdb_loader testcase.rdb
```

### AOF Files

```bash
# Create AOF commands
printf '*2\r\n$6\r\nSELECT\r\n$1\r\n0\r\n' > testcase.aof
./bin/fuzz_aof_parser testcase.aof
```

## Integration with OSS-Fuzz

OSS-Fuzz is a continuous fuzzing service for open source software.

### Setup

1. Create `project.yaml` in OSS-Fuzz repository
2. Add `build.sh` and `Dockerfile`
3. Submit PR to OSS-Fuzz

Example build script integration:

```bash
# In OSS-Fuzz build.sh
cd $SRC/valkey/tests/fuzz
./build_fuzz.sh ossfuzz
```

## Analyzing Crashes

### When a crash is found:

```bash
# LibFuzzer saves crashes to crash-* files
ls crash-*

# Reproduce the crash
./fuzz_resp_parser crash-abc123def456

# Debug with gdb
gdb --args ./fuzz_resp_parser crash-abc123def456
```

### Minimizing Test Cases

```bash
# Reduce crash to minimal input
./fuzz_resp_parser \
  -minimize_crash=1 \
  -exact_artifact_path=minimized-crash \
  crash-abc123def456
```

## Continuous Fuzzing in CI

Example GitHub Actions integration:

```yaml
- name: Build fuzz targets
  run: |
    cd tests/fuzz
    ./build_fuzz.sh ossfuzz

- name: Run short fuzz test
  run: |
    # Run each fuzzer for 60 seconds
    for fuzzer in tests/fuzz/out/fuzz_*; do
      $fuzzer -max_total_time=60 -timeout=5
    done
```

## Best Practices

### 1. Sanitizers

Always enable sanitizers for better bug detection:

```bash
# AddressSanitizer (memory errors)
-fsanitize=address

# UndefinedBehaviorSanitizer (UB)
-fsanitize=undefined

# MemorySanitizer (uninitialized memory)
-fsanitize=memory
```

### 2. Input Validation

Fuzz targets should validate input early to avoid wasting cycles:

```c
if (size == 0 || size > MAX_INPUT_SIZE) {
    return 0;  // Skip invalid inputs
}
```

### 3. Resource Limits

Prevent resource exhaustion:

```c
// Limit iterations
if (iteration_count > MAX_ITERATIONS) {
    return 0;
}

// Limit memory allocations
if (allocation_size > MAX_ALLOCATION) {
    return 0;
}
```

### 4. Determinism

Ensure fuzzing is deterministic for reproducibility:

- Avoid randomness (or use seeded PRNG)
- Avoid time-dependent behavior
- Clean up state between iterations

## Troubleshooting

### Slow Fuzzing

```bash
# Check execution speed
./fuzz_resp_parser -runs=10000

# Expected: >1000 exec/s
# If slower, optimize target or reduce instrumentation
```

### Out of Memory

```bash
# Limit memory usage
./fuzz_resp_parser -rss_limit_mb=2048

# Reduce max input length
./fuzz_resp_parser -max_len=10240
```

### Timeouts

```bash
# Increase timeout
./fuzz_resp_parser -timeout=30

# Or optimize target to handle large inputs faster
```

## Further Reading

- [LibFuzzer Documentation](https://llvm.org/docs/LibFuzzer.html)
- [OSS-Fuzz](https://google.github.io/oss-fuzz/)
- [AFL++ Fuzzer](https://github.com/AFLplusplus/AFLplusplus)
- [RFC 0004: Advanced Testing Infrastructure](../../rfcs/0004-advanced-testing-infrastructure.md)
- [Fuzzing Best Practices](https://github.com/google/fuzzing/blob/master/docs/good-fuzz-target.md)
