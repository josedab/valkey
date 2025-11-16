# Valkey RFCs (Requests for Comments)

This directory contains formal proposals for enhancements to the Valkey project. Each RFC follows a structured format to facilitate community discussion and TSC decision-making.

## RFC Process

1. **Draft**: Author creates RFC with detailed proposal
2. **Discussion**: Community reviews and provides feedback via GitHub Discussions
3. **Revision**: Author incorporates feedback and updates RFC
4. **TSC Review**: Technical Steering Committee evaluates proposal
5. **Decision**: TSC votes on acceptance (requires simple majority)
6. **Implementation**: Accepted RFCs move to implementation phase

## RFC Format

Each RFC contains:
- **Summary**: Brief overview of the proposal
- **Motivation**: Why this change is needed
- **Detailed Design**: Technical specification with code examples
- **Drawbacks**: Potential downsides and mitigations
- **Alternatives**: Other approaches considered
- **Unresolved Questions**: Open issues for discussion
- **Implementation Plan**: Milestones and timeline
- **Success Metrics**: How to measure success

## Active RFCs

### Priority 0 (Critical Impact - Start Immediately)

| RFC | Title | Status | Estimated Effort | Impact Score |
|-----|-------|--------|------------------|--------------|
| [0001](0001-enhanced-observability-metrics.md) | Enhanced Observability and Distributed Tracing | Draft | 8-11 weeks | 9/10 |
| [0002](0002-intelligent-memory-prefetching.md) | Intelligent Memory Prefetching and Cache Optimization | Draft | 9-12 weeks | 9/10 |

### Priority 1 (High Impact - Start Soon)

| RFC | Title | Status | Estimated Effort | Impact Score |
|-----|-------|--------|------------------|--------------|
| [0003](0003-structured-logging-framework.md) | Structured Logging Framework with LogFmt and JSON | Draft | 6-8 weeks | 8/10 |
| [0004](0004-advanced-testing-infrastructure.md) | Advanced Testing Infrastructure | Draft | 10-12 weeks | 8/10 |

### Priority 2 (Significant Impact)

| RFC | Title | Status | Estimated Effort | Impact Score |
|-----|-------|--------|------------------|--------------|
| [0005](0005-dynamic-io-thread-tuning.md) | Dynamic I/O Thread Auto-Tuning | Draft | 6-8 weeks | 7/10 |
| [0006](0006-async-signal-safe-crash-handler.md) | Async-Signal-Safe Crash Handler | Draft | 4-5 weeks | 7/10 |

### Priority 3 (Valuable Enhancements)

| RFC | Title | Status | Estimated Effort | Impact Score |
|-----|-------|--------|------------------|--------------|
| [0007](0007-simd-optimizations.md) | SIMD Optimizations for Hot Paths | Draft | 8-10 weeks | 6/10 |
| [0008](0008-configuration-validation.md) | Configuration Validation and Migration Framework | Draft | 5-6 weeks | 6/10 |
| [0009](0009-api-documentation-generator.md) | API Documentation Generator | Draft | 3-4 weeks | 5/10 |
| [0010](0010-cluster-topology-visualizer.md) | Cluster Topology Visualizer | Draft | 6-8 weeks | 5/10 |

### Priority 4 (Long-Term Initiatives)

| RFC | Title | Status | Estimated Effort | Impact Score |
|-----|-------|--------|------------------|--------------|
| [0011](0011-modern-c-standards-migration.md) | Modern C Standards Migration (C17/C23) | Draft | 20-30 weeks | 4/10 |
| [0012](0012-ebpf-dynamic-tracing.md) | eBPF-Based Dynamic Tracing | Draft | 10-15 weeks | 4/10 |

### Additional Proposals

| RFC | Title | Status | Estimated Effort | Impact Score |
|-----|-------|--------|------------------|--------------|
| [0013](0013-cluster-metadata-wal.md) | Write-Ahead Log for Cluster Metadata | Draft | 8-10 weeks | 7/10 |
| [0014](0014-todo-improvements.md) | Quick Wins from TODO Analysis | Draft | 1-2 weeks each | Varies |

## Roadmap

### Year 1 Focus Areas

| Quarter | Primary Focus | Expected Deliverables |
|---------|---------------|----------------------|
| **Q1** | Observability | Prometheus metrics, structured logging, OpenTelemetry integration |
| **Q2** | Performance | Memory prefetching, I/O tuning, SIMD optimizations |
| **Q3** | Testing & Quality | Property-based tests, fuzzing, benchmarks, crash handling |
| **Q4** | Developer Experience | Config validation, API docs, cluster visualizer |

### Expected Cumulative Impact

- **Performance**: 20-35% improvement (combined optimizations)
- **Reliability**: 30% reduction in production bugs (testing improvements)
- **Observability**: 50% faster issue diagnosis
- **Developer Experience**: Easier onboarding and contribution

## Contributing

To propose a new RFC:

1. **Check Existing RFCs**: Ensure your idea isn't already covered
2. **Create Draft**: Copy `0000-template.md` (if exists) or use existing RFC as template
3. **Number Your RFC**: Use next available sequential number
4. **Submit PR**: Create pull request with your RFC
5. **Engage Discussion**: Respond to community feedback
6. **Iterate**: Update RFC based on input

## RFC Template Structure

```markdown
# RFC NNNN: Title

## Summary
Brief 2-3 sentence overview

## Motivation
### Current State
### Problems Identified
### Use Cases

## Detailed Design
Technical specification with code examples

## Drawbacks
Potential issues and mitigations

## Alternatives
Other approaches considered and why rejected

## Unresolved Questions
Open questions for community discussion

## Implementation Plan
Milestones with timelines

## Success Metrics
Quantitative and qualitative measures

## References
Related work, papers, implementations

## Changelog
Version history
```

## Governance

- **RFC Acceptance**: Requires TSC approval (simple majority vote per [GOVERNANCE.md](../GOVERNANCE.md))
- **Major Decisions**: RFCs proposing breaking changes require 2/3 majority
- **Community Input**: All RFCs open for public discussion before TSC vote
- **Implementation**: Accepted RFCs create tracking issues for implementation

## References

- [Valkey Governance](../GOVERNANCE.md)
- [Contributing Guide](../CONTRIBUTING.md)
- [Valkey Improvement Proposals Document](../valkey-improvement-proposals.md)
- [Blog Post Series Plan](../blog-posts-plan.md)

## Changelog

- **2025-11-16**: Initial RFC directory created with 14 proposals based on comprehensive codebase analysis

---

**Note**: These RFCs are based on detailed analysis of the Valkey codebase (~100K lines of C), comparison with industry best practices from Apache Cassandra, ScyllaDB, DragonflyDB, and KeyDB, and identification of specific improvement opportunities through code review and TODO analysis.