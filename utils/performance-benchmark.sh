#!/bin/bash
# Performance benchmarking script for Valkey
#
# This script runs a comprehensive set of benchmarks and outputs results in JSON format.
# It is designed to be used for detecting performance regressions in CI/CD pipelines.

set -e

RESULTS_FILE="${1:-benchmark_results.json}"
VALKEY_BENCHMARK="${VALKEY_BENCHMARK:-valkey-benchmark}"
HOST="${VALKEY_HOST:-127.0.0.1}"
PORT="${VALKEY_PORT:-6379}"

# Function to convert valkey-benchmark output to JSON
benchmark_to_json() {
    local name="$1"
    local output="$2"

    echo "    {"
    echo "      \"name\": \"$name\","
    echo "      \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
    echo "      \"output\": $(echo "$output" | python3 -c "import sys, json; print(json.dumps(sys.stdin.read()))")"
    echo "    }"
}

# Check if valkey server is running
if ! timeout 5 bash -c "</dev/tcp/$HOST/$PORT" 2>/dev/null; then
    echo "Error: Valkey server not reachable at $HOST:$PORT" >&2
    exit 1
fi

echo "{"
echo "  \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
echo "  \"host\": \"$HOST\","
echo "  \"port\": $PORT,"
echo "  \"benchmarks\": ["

# Basic operations benchmark
echo "    {"
echo "      \"name\": \"basic_ops\","
echo "      \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
BASIC_OUTPUT=$($VALKEY_BENCHMARK -h "$HOST" -p "$PORT" -q -n 100000 -c 50 -P 16 \
  -t GET,SET,INCR,LPUSH,RPUSH,LPOP,RPOP,SADD,HSET,ZADD 2>&1 || echo "Benchmark failed")
echo "      \"output\": $(echo "$BASIC_OUTPUT" | python3 -c "import sys, json; print(json.dumps(sys.stdin.read()))")"
echo "    },"

# Latency percentiles
echo "    {"
echo "      \"name\": \"latency_percentiles\","
echo "      \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
LATENCY_OUTPUT=$($VALKEY_BENCHMARK -h "$HOST" -p "$PORT" -q -n 100000 -c 50 --csv \
  -t GET,SET 2>&1 | head -20 || echo "Benchmark failed")
echo "      \"output\": $(echo "$LATENCY_OUTPUT" | python3 -c "import sys, json; print(json.dumps(sys.stdin.read()))")"
echo "    },"

# Pipeline efficiency
echo "    {"
echo "      \"name\": \"pipeline_efficiency\","
echo "      \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
PIPELINE_OUTPUT=$($VALKEY_BENCHMARK -h "$HOST" -p "$PORT" -q -n 100000 -c 50 -P 64 \
  -t GET,SET 2>&1 || echo "Benchmark failed")
echo "      \"output\": $(echo "$PIPELINE_OUTPUT" | python3 -c "import sys, json; print(json.dumps(sys.stdin.read()))")"
echo "    },"

# Large value operations
echo "    {"
echo "      \"name\": \"large_values\","
echo "      \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
LARGE_OUTPUT=$($VALKEY_BENCHMARK -h "$HOST" -p "$PORT" -q -n 10000 -c 50 \
  -d 10240 -t GET,SET 2>&1 || echo "Benchmark failed")
echo "      \"output\": $(echo "$LARGE_OUTPUT" | python3 -c "import sys, json; print(json.dumps(sys.stdin.read()))")"
echo "    },"

# List operations
echo "    {"
echo "      \"name\": \"list_ops\","
echo "      \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
LIST_OUTPUT=$($VALKEY_BENCHMARK -h "$HOST" -p "$PORT" -q -n 100000 -c 50 \
  -t LPUSH,RPUSH,LPOP,RPOP,LRANGE 2>&1 || echo "Benchmark failed")
echo "      \"output\": $(echo "$LIST_OUTPUT" | python3 -c "import sys, json; print(json.dumps(sys.stdin.read()))")"
echo "    },"

# Set operations
echo "    {"
echo "      \"name\": \"set_ops\","
echo "      \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
SET_OUTPUT=$($VALKEY_BENCHMARK -h "$HOST" -p "$PORT" -q -n 100000 -c 50 \
  -t SADD,SPOP,SMEMBERS 2>&1 || echo "Benchmark failed")
echo "      \"output\": $(echo "$SET_OUTPUT" | python3 -c "import sys, json; print(json.dumps(sys.stdin.read()))")"
echo "    },"

# Sorted set operations
echo "    {"
echo "      \"name\": \"sorted_set_ops\","
echo "      \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
ZSET_OUTPUT=$($VALKEY_BENCHMARK -h "$HOST" -p "$PORT" -q -n 100000 -c 50 \
  -t ZADD,ZRANGE,ZREM 2>&1 || echo "Benchmark failed")
echo "      \"output\": $(echo "$ZSET_OUTPUT" | python3 -c "import sys, json; print(json.dumps(sys.stdin.read()))")"
echo "    },"

# Hash operations
echo "    {"
echo "      \"name\": \"hash_ops\","
echo "      \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
HASH_OUTPUT=$($VALKEY_BENCHMARK -h "$HOST" -p "$PORT" -q -n 100000 -c 50 \
  -t HSET,HGET,HMSET,HMGET 2>&1 || echo "Benchmark failed")
echo "      \"output\": $(echo "$HASH_OUTPUT" | python3 -c "import sys, json; print(json.dumps(sys.stdin.read()))")"
echo "    }"

echo "  ]"
echo "}"
