# Directory Structure & Naming Conventions - Visual Summary

## Output Directory Structure (After Refactoring)

```
myTest/
│
├── gen_workload.py                              # Refactored: CommonTrafficParams + StrideBasedNVMGenerator
├── run_cxl_test.py                             # Updated: stride-based config file naming
├── auto_script.py                              # Updated: stride-based test parameters
│
└── [Test Output Directories - Stride-Based NVM]
│
├── m5out_v2_nvm_stride64_0/                    # stride=64, lat=0, host_dram=8GiB
│   ├── traffic_stride_64.cfg                   # ← Clear stride value in filename
│   ├── simulation_config.json
│   ├── stats.txt
│   ├── gem5.log
│   └── m5out/
│       ├── system.terminal
│       └── stats.txt
│
├── m5out_v2_nvm_stride64_256/                  # stride=64, lat=256, host_dram=6GiB
│   ├── traffic_stride_64.cfg
│   ├── simulation_config.json
│   └── ...
│
├── m5out_v2_nvm_stride4096_0/                  # stride=4096, lat=0, host_dram=8GiB
│   ├── traffic_stride_4096.cfg                 # ← Different stride value
│   ├── simulation_config.json
│   └── ...
│
├── m5out_v2_nvm_stride4096_256/                # stride=4096, lat=256, host_dram=6GiB
│   ├── traffic_stride_4096.cfg
│   ├── simulation_config.json
│   └── ...
│
│
└── [Test Output Directories - YCSB (Optional)]
│
├── m5out_v2_ycsb_wA_random_0/                  # YCSB workload A, RANDOM mode, lat=0
│   ├── traffic_workload_A_random.cfg           # ← Clear workload type and mode
│   ├── simulation_config.json
│   └── ...
│
└── m5out_v2_ycsb_wB_linear_256/                # YCSB workload B, LINEAR mode, lat=256
    ├── traffic_workload_B_linear.cfg           # ← Different workload configuration
    ├── simulation_config.json
    └── ...
```

---

## Config File Naming Convention

### Before Refactoring (Problematic)
```
traffic_random.cfg      ← Doesn't clearly indicate stride value
traffic_linear.cfg      ← Misleading - not really linear!
traffic_<unclear>.cfg
```

**Problems:**
- ❌ Unclear relationship between filename and actual access pattern
- ❌ Can't distinguish between stride=64 and stride=4096 from filename alone
- ❌ "Random/Linear" doesn't reflect actual hardware behavior

### After Refactoring (Clear & Precise)
```
traffic_stride_64.cfg           ← Cache-line aligned access
traffic_stride_128.cfg          ← Cache-line variant
traffic_stride_4096.cfg         ← Page-aligned access
traffic_workload_A_random.cfg   ← YCSB workload type A, random access
traffic_workload_B_linear.cfg   ← YCSB workload type B, linear access
```

**Benefits:**
- ✅ Filename precisely describes the workload
- ✅ No ambiguity about access pattern
- ✅ Easy to cross-reference with test metadata
- ✅ Self-documenting code

---

## Test Naming Convention

### NVM Tests (Stride-Based Classification)
```
m5out_v2_nvm_stride<STRIDE>_<LAT>

Components:
  v2          = Version/run number
  nvm         = Workload type (NVM)
  stride<X>   = Memory access stride in bytes (64, 128, 4096, etc.)
  <LAT>       = CXL large access threshold (0, 256, etc.)

Examples:
  m5out_v2_nvm_stride64_0        → stride=64B, lat=0ns
  m5out_v2_nvm_stride64_256      → stride=64B, lat=256ns
  m5out_v2_nvm_stride4096_0      → stride=4096B, lat=0ns
  m5out_v2_nvm_stride4096_256    → stride=4096B, lat=256ns
```

### YCSB Tests (Type + Mode Classification)
```
m5out_v2_ycsb_w<TYPE>_<MODE>_<LAT>

Components:
  v2         = Version/run number
  ycsb       = Workload type (YCSB)
  w<TYPE>    = YCSB workload type (A, B, C, etc.)
  <MODE>     = Access pattern (random, linear)
  <LAT>      = CXL large access threshold

Examples:
  m5out_v2_ycsb_wA_random_0      → YCSB-A, random, lat=0ns
  m5out_v2_ycsb_wB_linear_256    → YCSB-B, linear, lat=256ns
```

---

## Class Hierarchy & Instantiation

### New Architecture (DRY Principle Applied)

```
┌─────────────────────────────────────────────────┐
│      CommonTrafficParams                        │
│  (Shared Configuration - Single Source)         │
│                                                 │
│  • block_size: 64 bytes                        │
│  • min_period: 64 cycles                       │
│  • max_period: 128 cycles                      │
│  • hot_ratio: 0.2 (20% of address space)       │
│  • prob_hot: 0.8                               │
│  • prob_cold: 0.2                              │
└─────────────────────────────────────────────────┘
           ▲
           │ uses (dependency)
           │
    ┌──────┴──────────────────────────┐
    │                                 │
    │                                 │
┌───────────────────┐    ┌──────────────────────────────────┐
│  TrafficGenConfig │    │                                  │
│   (Abstract)      │    │ Provides framework & templates:  │
│                   │    │  • _write_header()              │
│  • generate()     │    │  • _write_transitions()         │
│  • _calc_regions()│    │  • _get_state_config() [abstract]
└────┬──────────────┘    │                                  │
     │                   └──────────────────────────────────┘
     │ (ABC)
     │ (inherits)
     │
     ├───────────────────────┬────────────────────────────────┐
     │                       │                                │
     ▼                       ▼                                ▼
┌─────────────────┐  ┌──────────────────────┐    ┌──────────────────────┐
│ YCSBGenerator   │  │ StrideBasedNVM       │    │  Factory Function    │
│                 │  │ Generator            │    │  create_config()     │
│ • workload_type │  │                      │    │                      │
│ • workload_mode │  │ • stride (64, 4096)  │    │ Routes to correct    │
│                 │  │                      │    │ generator based on   │
│ Config: traffic_│  │ Config: traffic_     │    │ "kind" parameter     │
│ workload_<T>_   │  │ stride_<S>.cfg       │    │                      │
│ <M>.cfg         │  │                      │    │ Returns: filename    │
└─────────────────┘  └──────────────────────┘    └──────────────────────┘
```

---

## Data Flow: Test Definition → Output

```
┌─────────────────────────────────────────────────────────────────┐
│ auto_script.py - Test Definition                                │
│                                                                 │
│ TESTS = [                                                       │
│   {"lat": "0", "kind": "nvm", "stride": 64, ...},              │
│   {"lat": "256", "kind": "nvm", "stride": 64, ...},            │
│   {"lat": "0", "kind": "nvm", "stride": 4096, ...},            │
│   {"lat": "256", "kind": "nvm", "stride": 4096, ...}           │
│ ]                                                               │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│ Test Name Generation & Directory Creation                       │
│                                                                 │
│ m5out_v2_nvm_stride64_0/                                        │
│ m5out_v2_nvm_stride64_256/                                      │
│ m5out_v2_nvm_stride4096_0/                                      │
│ m5out_v2_nvm_stride4096_256/                                    │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│ run_cxl_test.py Execution (per test)                            │
│                                                                 │
│ Arguments:                                                      │
│   --kind=nvm                                                    │
│   --stride=64 (or 4096)                                         │
│   --lat=0 (or 256)                                              │
│   --save-dir=./m5out_v2_nvm_stride64_0                          │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│ create_config() Factory Function                                │
│                                                                 │
│ 1. Parse kind="nvm"                                             │
│ 2. Create filename: traffic_stride_64.cfg                       │
│ 3. Instantiate StrideBasedNVMGenerator                          │
│ 4. Call generate() → writes .cfg file                           │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│ Output Files in m5out_v2_nvm_stride64_0/                        │
│                                                                 │
│ ✓ traffic_stride_64.cfg          (Generated config)            │
│ ✓ simulation_config.json         (Metadata)                    │
│ ✓ gem5.log, stats.txt            (gem5 outputs)                │
└─────────────────────────────────────────────────────────────────┘
```

---

## Stride Value Interpretation Table

| Stride | Use Case | Hardware Relevance | Example Access Pattern |
|--------|----------|-------------------|------------------------|
| 64     | Cache-line aligned | Standard L1/L2 cache lines (x86, ARM) | Sequential memory reads |
| 128    | Double cache-line | Some ARM variants or SIMD operations | Vectorized access |
| 256    | L3 cache aligned | Some processor architectures | Large block transfers |
| 4096   | Page-aligned | Virtual memory page size (4KB pages) | Page-based workloads |
| 8192   | Large page aligned | THP (Transparent Huge Pages) | Huge page optimizations |
| 16384  | Larger pages | Virtual page sizes on some systems | Multi-level paging |

---

## Migration Path: Old Naming → New Naming

### Example 1: Stride-Based NVM Workloads

**Old Output:**
```
m5out_old/
├── traffic_random.cfg        ← Ambiguous!
└── simulation_config.json
```

**New Output:**
```
m5out_v2_nvm_stride64_0/
├── traffic_stride_64.cfg     ← Crystal clear!
└── simulation_config.json
```

### Example 2: Cross-Test Identification

**Old approach:**
- Operator: "Was this stride=64 or stride=4096?"
- Solution: Check simulation_config.json

**New approach:**
- Directory name says it all: `m5out_v2_nvm_stride4096_256`
- Quick identification without file inspection

### Example 3: Batch Processing

**Old batch script output:**
```
m5out_test_1/  traffic_random.cfg
m5out_test_2/  traffic_linear.cfg
m5out_test_3/  traffic_random.cfg    ← Which is this, really?
```

**New batch script output:**
```
m5out_v2_nvm_stride64_0/     traffic_stride_64.cfg
m5out_v2_nvm_stride4096_0/   traffic_stride_4096.cfg
m5out_v2_nvm_stride64_256/   traffic_stride_64.cfg
```

---

## Summary Table

| Feature | Before | After | Benefit |
|---------|--------|-------|---------|
| **NVM Classification** | Random / Linear | Stride value (64, 128, 4096, ...) | Precise, hardware-aligned |
| **Config Naming** | `traffic_random.cfg` | `traffic_stride_64.cfg` | Self-documenting |
| **Test Directory** | `m5out_test_1/` | `m5out_v2_nvm_stride64_0/` | Clear parameters at a glance |
| **Base Parameters** | Duplicated in classes | `CommonTrafficParams` | DRY principle, easier maintenance |
| **Extensibility** | Need to modify code | Just add stride value to TESTS | Scalable configuration |

---

## Quick Reference

### To Run a Specific Test
```bash
cd /home/ben/Documents/io-exp/gem5/myTest

# NVM with stride=64, lat=0
python3 run_cxl_test.py --kind=nvm --stride=64 --lat=0 --save-dir=./m5out_v2_nvm_stride64_0

# NVM with stride=4096, lat=256
python3 run_cxl_test.py --kind=nvm --stride=4096 --lat=256 --save-dir=./m5out_v2_nvm_stride4096_256

# YCSB workload A, random, lat=0 (optional)
python3 run_cxl_test.py --kind=ycsb --workload=A --workload_mode=RANDOM --lat=0
```

### To Run All Tests
```bash
python3 auto_script.py
```

### To Analyze Results
```bash
# All output directories follow the naming convention:
ls -d m5out_v2_nvm_stride*/
# Output:
# m5out_v2_nvm_stride64_0/
# m5out_v2_nvm_stride64_256/
# m5out_v2_nvm_stride4096_0/
# m5out_v2_nvm_stride4096_256/

# Inspect configuration used in a test
cat m5out_v2_nvm_stride64_0/simulation_config.json
cat m5out_v2_nvm_stride64_0/traffic_stride_64.cfg
```

---

## File Sizes & Output Organization

```
m5out_v2_nvm_stride64_0/
├── traffic_stride_64.cfg              (~0.5 KB)    Generated config
├── simulation_config.json             (~1 KB)      Test parameters (JSON)
├── gem5.log                           (~10-50 MB)  gem5 detailed log
├── stats.txt                          (~1-5 MB)    Performance statistics
└── m5out/
    ├── system.terminal                (~50-500 KB) System messages
    └── stats.txt                      (~duplicated stats)

Total per test: ~50-100 MB (depending on simulation length)
```

