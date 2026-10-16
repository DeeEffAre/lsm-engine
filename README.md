## Project Status & Active Development

> **Note:** This project is under active development. The primary goal is to build an idiomatic, crash-resilient, high-throughput LSM-tree storage engine from scratch in modern C++, focusing on cache-friendly data layouts, zero-copy serialization, and low-latency access paths.

---

## Roadmap

Below is the current development progress and target milestones.

### Phase 1: Core Engine & In-Memory Storage
- [x] **Memtable**: Concurrent-ready Skiplist backed implementation using Pimpl idiom
- [x] **SSTable Foundation**: Append-only SSTable writer and reader with length-prefixed records
- [x] **Flush Engine**: Coordinated Memtable flush to disk with atomic rotation and monotonic SSTable counters
- [x] **Database Interface**: Typed `Database<K, V>` wrapper over the raw storage engine
- [x] **Point Lookups**: Newest-to-oldest SSTable traversal for point queries (`get`)
- [x] **Startup Recovery**: Automatic discovery of existing SSTables and counter synchronization on engine boot

### Phase 2: Serialization & Binary Codec
- [x] **Framed Framing**: Binary framing format with magic bytes, schema versioning, and length prefixes
- [x] **Zero-Copy Views**: Deserialization using non-owning views (`std::string_view`) to eliminate allocation overhead
- [x] **Error Handling**: Explicit, zero-cost error modeling powered by `std::expected`
- [x] **Compact Encodings**: Varint / LEB128 variable-length integer encoding for size efficiency
- [x] **Type Safety**: Modern C++ concepts and compile-time constraints (`static_assert`)
- [ ] **Endian Independence**: Strict Little-Endian canonical wire format across platforms
- [ ] **Type Support**: Generic traits/ADL-based serialization for user types and standard containers (`std::vector`, `std::optional`, `std::pair`)
- [ ] **Robustness**: Fuzzing decode pathways against truncated and adversarial inputs

### Phase 3: Durability & Crash Safety
- [x] **Write-Ahead Logging (WAL)**: Append-only durability log with replay recovery on startup
- [x] **Torn-Write Recovery**: Resilient replay parser that safely recovers up to the last valid log entry
- [ ] **Checksumming**: Block and record-level CRC32C validation for corruption detection
- [ ] **Atomic File Operations**: Safe SSTable staging via temporary files and atomic directory renames

### Phase 4: LSM Enhancements & Read/Write Optimizations
- [ ] **Deletions & Tombstones**: Point tombstone semantics with short-circuit evaluation in read paths
- [ ] **Sparse Indexing**: Primary key block index to replace linear scans within SSTables
- [ ] **Probabilistic Filtering**: Block/Table-level Bloom filters to avoid unnecessary disk I/O on negative lookups
- [ ] **Iterators**: Merging / Two-way sorted iterators supporting efficient range scans and prefix queries
- [ ] **Compaction Subsystem**: Size-tiered compaction progressing to leveled compaction (LSM tree maintenance)
- [ ] **Manifest & Version Edit**: Atomic state tracking of active SSTable sets and level hierarchies
- [ ] **Concurrency & Background Jobs**: Dedicated worker threads for asynchronous flushing and compaction with write-stall throttling

### Phase 5: Testing, Tooling & Benchmarks
- [x] **Testing Harness**: Unit testing suite powered by Catch2, fully integrated with CTest and CMake presets
- [x] **Sanitizers**: ASan (AddressSanitizer) and UBSan (UndefinedBehaviorSanitizer) integrated build targets
- [ ] **Stress & Chaos Testing**: Simulated crash recovery (`kill -9`, mid-write truncation) and property-based verification against baseline containers
- [ ] **CI Pipeline**: Automated multi-compiler (Clang/GCC) workflows on Linux via GitHub Actions
- [ ] **Micro & Macro Benchmarks**: Performance harnesses using Google Benchmark (measuring sequential/random read/write throughput and latency distribution)
- [ ] **Comparative Analysis**: Head-to-head performance comparisons against LevelDB / RocksDB on standard workloads
