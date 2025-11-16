#!/bin/bash
# Build script for fuzz targets
#
# This script builds all fuzz targets for both local testing and OSS-Fuzz integration.
#
# Usage:
#   ./build_fuzz.sh [standalone|ossfuzz]
#
# Modes:
#   standalone - Build standalone executables for local testing
#   ossfuzz    - Build for OSS-Fuzz integration (default)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$SCRIPT_DIR/../.."
BUILD_MODE="${1:-ossfuzz}"

cd "$SCRIPT_DIR"

# Detect available compilers
if command -v clang &> /dev/null; then
    CC=clang
elif command -v gcc &> /dev/null; then
    CC=gcc
else
    echo "Error: No C compiler found (clang or gcc required)"
    exit 1
fi

echo "Building fuzz targets with $CC in $BUILD_MODE mode"

# Build configurations
if [ "$BUILD_MODE" = "standalone" ]; then
    echo "Building standalone fuzz targets..."

    # Standalone mode - build with FUZZ_STANDALONE flag
    CFLAGS="-g -O0 -DFUZZ_STANDALONE"
    LDFLAGS=""
    OUTPUT_DIR="$SCRIPT_DIR/bin"

    mkdir -p "$OUTPUT_DIR"

    for fuzz_target in "$SCRIPT_DIR"/fuzz_*.c; do
        if [ ! -f "$fuzz_target" ]; then
            continue
        fi

        target_name=$(basename "$fuzz_target" .c)
        echo "  Building $target_name (standalone)..."

        $CC $CFLAGS \
            -I"$SRC_DIR/src" \
            "$fuzz_target" \
            -o "$OUTPUT_DIR/$target_name" \
            $LDFLAGS

        echo "  → $OUTPUT_DIR/$target_name"
    done

    echo ""
    echo "Standalone fuzz targets built successfully in $OUTPUT_DIR"
    echo "Run with: $OUTPUT_DIR/fuzz_resp_parser <input_file>"

elif [ "$BUILD_MODE" = "ossfuzz" ]; then
    echo "Building for OSS-Fuzz integration..."

    # OSS-Fuzz mode - use environment variables set by OSS-Fuzz
    : ${CC:=clang}
    : ${CXX:=clang++}
    : ${CFLAGS:=-fsanitize=fuzzer-no-link,address -g}
    : ${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}
    : ${OUT:=$SCRIPT_DIR/out}

    mkdir -p "$OUT"

    echo "  CC: $CC"
    echo "  CFLAGS: $CFLAGS"
    echo "  LIB_FUZZING_ENGINE: $LIB_FUZZING_ENGINE"
    echo "  OUT: $OUT"

    # Build each fuzz target
    for fuzz_target in "$SCRIPT_DIR"/fuzz_*.c; do
        if [ ! -f "$fuzz_target" ]; then
            continue
        fi

        target_name=$(basename "$fuzz_target" .c)
        echo "  Building $target_name (OSS-Fuzz)..."

        $CC $CFLAGS $LIB_FUZZING_ENGINE \
            -I"$SRC_DIR/src" \
            "$fuzz_target" \
            -o "$OUT/$target_name"

        echo "  → $OUT/$target_name"
    done

    # Create seed corpora directories
    echo ""
    echo "Creating seed corpora..."

    for fuzz_target in "$SCRIPT_DIR"/fuzz_*.c; do
        target_name=$(basename "$fuzz_target" .c)
        corpus_dir="$OUT/${target_name}_seed_corpus"
        mkdir -p "$corpus_dir"

        # Create basic seed files based on target type
        case "$target_name" in
            fuzz_resp_parser)
                # Simple RESP commands
                echo -e '*1\r\n$4\r\nPING\r\n' > "$corpus_dir/ping.resp"
                echo -e '*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n' > "$corpus_dir/get.resp"
                echo -e '*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n' > "$corpus_dir/set.resp"
                ;;

            fuzz_cluster_message)
                # Basic cluster message header
                printf 'RCmb' > "$corpus_dir/header.bin"
                ;;

            fuzz_rdb_loader)
                # Simple RDB file
                echo -en 'REDIS0011' > "$corpus_dir/simple.rdb"
                echo -en '\xFE\x00\xFF' >> "$corpus_dir/simple.rdb"
                ;;

            fuzz_aof_parser)
                # Simple AOF commands
                echo -e '*2\r\n$6\r\nSELECT\r\n$1\r\n0\r\n' > "$corpus_dir/select.aof"
                echo -e '*3\r\n$3\r\nset\r\n$1\r\na\r\n$1\r\nb\r\n' > "$corpus_dir/set.aof"
                ;;
        esac

        echo "  → $corpus_dir"
    done

    echo ""
    echo "OSS-Fuzz targets built successfully in $OUT"
    echo "Run with: $OUT/fuzz_resp_parser"

else
    echo "Error: Unknown build mode '$BUILD_MODE'"
    echo "Usage: $0 [standalone|ossfuzz]"
    exit 1
fi

echo ""
echo "Build complete!"
