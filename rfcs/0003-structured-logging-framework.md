# RFC 0003: Structured Logging Framework with LogFmt and JSON

## Summary

This RFC proposes implementing a comprehensive structured logging framework for Valkey that supports LogFmt and JSON output formats, enabling easier log parsing, aggregation, and analysis in modern observability stacks. The framework will provide rich contextual information for debugging distributed operations while maintaining backward compatibility with traditional logging.

## Motivation

### Current State

Valkey currently uses traditional syslog-style logging:
- Text-based log messages in [`server.h`](src/server.h:616-617)
- Basic logfmt support flag added but not fully implemented
- Limited structured context in log messages
- Difficult to parse programmatically for automated analysis

### Problems Identified

1. **Log Parsing Complexity**: Unstructured text logs require complex regex patterns to extract information
2. **Missing Context**: Critical context often missing (client ID, slot number, command name, trace IDs)
3. **Cluster Correlation**: Hard to correlate events across cluster nodes without structured identifiers
4. **Integration Gaps**: Doesn't integrate well with modern log aggregation tools (ELK, Splunk, Datadog)
5. **Performance Debugging**: Latency and performance metrics not included in relevant log entries

### Use Cases

- **Distributed Debugging**: Correlate log events across multiple cluster nodes using trace IDs
- **Automated Alerting**: Parse structured logs to trigger alerts on specific error conditions
- **Performance Analysis**: Extract latency metrics from logs for trend analysis
- **Audit Trails**: Capture complete context for security and compliance auditing
- **Client Troubleshooting**: Identify problematic clients by ID and trace their operations

## Detailed Design

### Core Structured Logging Infrastructure

```c
// New file: src/structured_log.h

#ifndef __STRUCTURED_LOG_H
#define __STRUCTURED_LOG_H

#include "server.h"

/* Log field types for structured logging */
typedef enum {
    LOG_FIELD_STRING,
    LOG_FIELD_INTEGER,
    LOG_FIELD_UNSIGNED,
    LOG_FIELD_DOUBLE,
    LOG_FIELD_BOOL,
    LOG_FIELD_MSTIME,
    LOG_FIELD_CLIENT_ID,
    LOG_FIELD_NODE_ID,
    LOG_FIELD_TRACE_ID,
    LOG_FIELD_SPAN_ID
} log_field_type;

/* Named log field for structured logging */
typedef struct log_field {
    const char *name;         /* Field name (e.g., "client_id") */
    log_field_type type;      /* Field value type */
    union {
        const char *str_value;
        long long int_value;
        unsigned long long uint_value;
        double double_value;
        int bool_value;
        mstime_t time_value;
    } value;
} log_field;

/* Structured log output formats */
typedef enum {
    LOG_FORMAT_TRADITIONAL,   /* Classic syslog format */
    LOG_FORMAT_LOGFMT,        /* key=value format */
    LOG_FORMAT_JSON           /* JSON objects */
} log_format;

/* Structured logging configuration */
typedef struct structured_log_config {
    log_format format;            /* Output format */
    int include_timestamp;        /* Include ISO8601 timestamp */
    int include_level;            /* Include log level */
    int include_source;           /* Include source file:line */
    int include_thread_id;        /* Include thread identifier */
    int include_process_id;       /* Include process ID */
    sds field_separator;          /* Separator for logfmt (default: " ") */
} structured_log_config;

/* Global structured logging config */
extern structured_log_config server_log_config;

/* Initialization */
void structuredLogInit(void);
void structuredLogCleanup(void);

/* Core structured logging function */
void logStructured(int level, const char *message, 
                   const log_field *fields, size_t field_count);

/* Format-specific renderers */
sds renderLogTraditional(int level, const char *message,
                        const log_field *fields, size_t field_count);
sds renderLogFmt(int level, const char *message,
                const log_field *fields, size_t field_count);
sds renderLogJson(int level, const char *message,
                 const log_field *fields, size_t field_count);

/* Convenience macros for common logging patterns */

/* Log with client context */
#define LOG_WITH_CLIENT(level, client, msg, ...) \
    do { \
        log_field _fields[] = { \
            {.name = "client_id", .type = LOG_FIELD_UNSIGNED, \
             .value.uint_value = (client)->id}, \
            {.name = "client_addr", .type = LOG_FIELD_STRING, \
             .value.str_value = getClientPeerId(client)}, \
            __VA_ARGS__ \
        }; \
        logStructured(level, msg, _fields, \
                     sizeof(_fields) / sizeof(log_field)); \
    } while(0)

/* Log cluster events with slot and node context */
#define LOG_CLUSTER_EVENT(level, slot, msg, ...) \
    do { \
        log_field _fields[] = { \
            {.name = "slot", .type = LOG_FIELD_INTEGER, \
             .value.int_value = slot}, \
            {.name = "node_id", .type = LOG_FIELD_STRING, \
             .value.str_value = server.cluster->myself->name}, \
            __VA_ARGS__ \
        }; \
        logStructured(level, msg, _fields, \
                     sizeof(_fields) / sizeof(log_field)); \
    } while(0)

/* Log command execution with timing */
#define LOG_COMMAND_EXECUTION(level, client, cmd, duration_us, msg, ...) \
    do { \
        log_field _fields[] = { \
            {.name = "client_id", .type = LOG_FIELD_UNSIGNED, \
             .value.uint_value = (client)->id}, \
            {.name = "command", .type = LOG_FIELD_STRING, \
             .value.str_value = (cmd)->fullname}, \
            {.name = "duration_us", .type = LOG_FIELD_UNSIGNED, \
             .value.uint_value = duration_us}, \
            __VA_ARGS__ \
        }; \
        logStructured(level, msg, _fields, \
                     sizeof(_fields) / sizeof(log_field)); \
    } while(0)

#endif /* __STRUCTURED_LOG_H */
```

### Implementation Examples

#### Client Timeout Logging

```c
/* Before: Traditional logging */
serverLog(LL_WARNING, "Client %llu timeout", (unsigned long long)c->id);

/* After: Structured logging with context */
LOG_WITH_CLIENT(LL_WARNING, c, "client timeout",
    {.name = "idle_time_ms", .type = LOG_FIELD_UNSIGNED,
     .value.uint_value = server.mstime - c->lastinteraction},
    {.name = "last_command", .type = LOG_FIELD_STRING,
     .value.str_value = c->lastcmd ? c->lastcmd->fullname : "none"}
);

/* LogFmt output:
ts=2025-11-16T08:00:00.123Z level=warning msg="client timeout" client_id=12345 client_addr="192.168.1.100:54321" idle_time_ms=30000 last_command="GET"
*/

/* JSON output:
{"timestamp":"2025-11-16T08:00:00.123Z","level":"warning","message":"client timeout","client_id":12345,"client_addr":"192.168.1.100:54321","idle_time_ms":30000,"last_command":"GET"}
*/
```

### Configuration Options

```c
/* New configuration options in valkey.conf */

# Structured logging format: traditional, logfmt, or json
# Default: traditional (backward compatible)
log-format traditional

# Include additional context in logs
log-include-timestamp yes
log-include-level yes
log-include-process-id no
log-include-thread-id no

# LogFmt specific options
logfmt-field-separator " "
```

## Drawbacks

### Performance Overhead

- **String Formatting**: Structured logging requires more string manipulation
  - **Mitigation**: Fast path for disabled log levels (no overhead)
  - **Estimated Impact**: <1% CPU overhead at normal log levels

- **Log Volume Increase**: Structured logs are more verbose
  - **Mitigation**: Configurable field inclusion
  - **Compression**: Better compression ratios with structured formats

## Alternatives

### Alternative 1: JSON-Only Logging

**Decision**: Support multiple formats with traditional as default for backward compatibility.

### Alternative 2: External Log Processor

**Decision**: In-process structured logging provides better context and lower latency.

## Unresolved Questions

1. **Log Sampling**: Should high-volume logs be sampled to reduce overhead?
2. **Sensitive Data**: How to handle logging of sensitive information?
3. **Dynamic Fields**: Should modules be able to register custom log fields?

## Implementation Plan

### Milestone 1: Core Infrastructure (Weeks 1-2)
- [ ] Implement `structured_log.h` data structures
- [ ] Add LogFmt and JSON renderers
- [ ] Add configuration options
- [ ] Unit tests

### Milestone 2: Integration (Weeks 3-4)
- [ ] Add logging macros for common patterns
- [ ] Migrate high-value log points
- [ ] Integration tests

### Milestone 3: Documentation (Weeks 5-6)
- [ ] User documentation
- [ ] Migration guide
- [ ] Example queries for log analysis tools

## Success Metrics

- **Adoption**: 40% of deployments enable structured logging within 6 months
- **Performance**: <1% CPU overhead
- **Integration**: Documentation for major log analysis tools

## References

- [LogFmt Specification](https://brandur.org/logfmt)
- [Structured Logging Best Practices](https://www.honeycomb.io/blog/structured-logging-and-your-team)
- [Current Valkey Logging](src/server.h:616-617)

## Changelog

- **2025-11-16**: Initial RFC draft