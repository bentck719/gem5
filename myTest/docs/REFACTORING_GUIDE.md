# Traffic Generator Refactoring Guide

## Overview

This document explains the refactored traffic generator system, which transitions from a "Random/Linear" classification model to a **stride-based classification** for NVM workloads, while implementing DRY (Don't Repeat Yourself) principles through a shared base configuration layer.

---

## Architecture Changes

### 1. **New Class Hierarchy: Base + Specific Pattern**

#### Before Refactoring
```
TrafficGenConfig (abstract)
├── YCSBGenerator
└── NVMGenerator
```

**Problem:** Common parameters (block_size, min_period, max_period) were duplicated in both subclasses.

#### After Refactoring
```
CommonTrafficParams
    └─ Holds: block_size, min_period, max_period, hot_ratio, prob_hot, prob_cold

TrafficGenConfig (abstract)
    ├── YCSBGenerator
    └── StrideBasedNVMGenerator

Factory: create_config(filename, kind, common_params, **kwargs)
```

**Benefit:** Single source of truth for shared parameters. Easy to customize via `CommonTrafficParams` instance.

---

## Class Descriptions

### `CommonTrafficParams`
**Purpose:** Encapsulates all parameters shared between YCSB and NVM generators.

**Parameters:**
- `block_size` (default: 64): Memory access block size in bytes
- `min_period` (default: 64): Minimum delay between requests
- `max_period` (default: 128): Maximum delay between requests
- `hot_ratio` (default: 0.2): Fraction of address space that is "hot"
- `prob_hot` (default: 0.8): Probability of transitioning to hot state
- `prob_cold` (default: 0.2): Probability of transitioning to cold state

**Usage Example:**
```python
custom_params = CommonTrafficParams(
    block_size=128,
    min_period=32,
    max_period=256,
    hot_ratio=0.1
)
gen = StrideBasedNVMGenerator(
    filename="traffic_stride_64.cfg",
    base_addr=0,
    size=1024,
    stride=64,
    common_params=custom_params
)
gen.generate()
```

### `TrafficGenConfig` (Abstract Base)
**Purpose:** Provides the framework for generating trafficgen config files.

**Key Methods:**
- `_calculate_regions()`: Splits address space into hot/cold regions
- `_write_header()`: Writes initialization block
- `_write_transitions()`: Writes state transition logic
- `generate()`: Main method to produce config file

### `YCSBGenerator`
**Purpose:** Generates YCSB-style workload configurations.

**Classification:** By workload type (A, B, etc.) and access pattern (RANDOM, LINEAR)

**Parameters:**
- `workload_type`: YCSB type (A=50R/50W, B=95R/5W)
- `workload_mode`: RANDOM or LINEAR access pattern
- Inherits: block_size, min_period, max_period from `CommonTrafficParams`

**Generated Config File:**
- Naming: `traffic_workload_<type>_<mode>.cfg`
- Example: `traffic_workload_A_random.cfg`

### `StrideBasedNVMGenerator`
**Purpose:** Generates NVM workloads classified by memory access stride.

**Classification:** By stride value (e.g., 64, 128, 4096 bytes)

**Stride Values & Their Meaning:**
- `stride=64`: Cache-line aligned access (typical for sequential access patterns)
- `stride=128`: Cache-line variant, useful for specific prefetching scenarios
- `stride=4096`: Page-aligned access (typical for larger page-based workloads)

**Parameters:**
- `stride`: Access stride size in bytes (replaces Random/Linear)
- Inherits: block_size, min_period, max_period from `CommonTrafficParams`

**Generated Config File:**
- Naming: `traffic_stride_<stride_value>.cfg`
- Examples: 
  - `traffic_stride_64.cfg`
  - `traffic_stride_4096.cfg`

---

## Stride-Based Classification System

### Why Stride-Based?

The old "Random/Linear" classification was too coarse. Stride-based classification provides:

1. **Specificity**: Exactly defines the memory access pattern
2. **Scalability**: Easy to add new stride values without code changes
3. **Physical Relevance**: Stride directly correlates to hardware behavior (cache lines, page sizes)

### Common Stride Values

| Stride | Use Case | Description |
|--------|----------|-------------|
| 64     | Cache-line access | Standard cache-line size on x86/ARM |
| 128    | Variant patterns  | 2× cache-line for specific prefetching |
| 4096   | Page-aligned access | Virtual page size on Linux |
| 8192   | Large page access  | Huge page optimizations |

---

## Folder Structure & Naming Convention

### New Directory Layout

```
myTest/
├── gen_workload.py                 # Refactored generator (CommonTrafficParams, StrideBasedNVMGenerator)
├── run_cxl_test.py                # Entry point with stride-based naming
├── auto_script.py                 # Batch runner with stride-based test definitions
│
└── m5out_v2_nvm_stride64_0/        # Example output directory (stride=64, lat=0)
    ├── traffic_stride_64.cfg       # Generated config file
    ├── simulation_config.json      # Metadata
    ├── stats.txt                   # gem5 statistics
    └── ...
    
└── m5out_v2_nvm_stride4096_256/    # Example output directory (stride=4096, lat=256)
    ├── traffic_stride_4096.cfg     # Generated config file
    ├── simulation_config.json      # Metadata
    ├── stats.txt                   # gem5 statistics
    └── ...
    
└── m5out_v2_ycsb_wA_random_0/      # YCSB example (workload A, RANDOM mode, lat=0)
    ├── traffic_workload_A_random.cfg
    ├── simulation_config.json      # Metadata
    ├── stats.txt                   # gem5 statistics
    └── ...
```

### Naming Convention

#### NVM Tests (Stride-Based)
```
m5out_<version>_nvm_stride<stride_value>_<lat>

Examples:
- m5out_v2_nvm_stride64_0
- m5out_v2_nvm_stride64_256
- m5out_v2_nvm_stride4096_0
- m5out_v2_nvm_stride4096_256
```

#### YCSB Tests (Type + Mode Based)
```
m5out_<version>_ycsb_w<workload>_<mode>_<lat>

Examples:
- m5out_v2_ycsb_wA_random_0
- m5out_v2_ycsb_wB_linear_256
```

#### Config Files
```
NVM:    traffic_stride_<stride_value>.cfg
YCSB:   traffic_workload_<type>_<mode>.cfg

Examples:
- traffic_stride_64.cfg
- traffic_stride_4096.cfg
- traffic_workload_A_random.cfg
- traffic_workload_B_linear.cfg
```

---

## Migration Guide: Old Code → New Code

### Old Way (YCSB)
```python
from gen_workload import YCSBGenerator

gen = YCSBGenerator(
    "config.cfg",
    base_addr=0,
    size=1024,
    workload_type="A",
    workload_mode="RANDOM"
)
gen.generate()
```

### New Way (YCSB - with Common Params)
```python
from gen_workload import YCSBGenerator, CommonTrafficParams

params = CommonTrafficParams(block_size=64, min_period=64)
gen = YCSBGenerator(
    "traffic_workload_A_random.cfg",
    base_addr=0,
    size=1024,
    workload_type="A",
    workload_mode="RANDOM",
    common_params=params
)
gen.generate()
```

### Old Way (NVM with Random/Linear)
```python
from gen_workload import NVMGenerator

gen = NVMGenerator(
    "traffic_random.cfg",
    base_addr=0,
    size=1024,
    stride=64  # But naming didn't reflect this
)
gen.generate()
```

### New Way (NVM with Stride-Based)
```python
from gen_workload import StrideBasedNVMGenerator, CommonTrafficParams

params = CommonTrafficParams()
gen = StrideBasedNVMGenerator(
    "traffic_stride_64.cfg",    # Clear stride in filename
    base_addr=0,
    size=1024,
    stride=64,
    common_params=params
)
gen.generate()
```

### Using Factory (Recommended)
```python
from gen_workload import create_config

# YCSB
create_config(
    filename="traffic_workload_A_random.cfg",
    kind="ycsb",
    base_addr=0,
    size=1024,
    workload_type="A",
    workload_mode="RANDOM"
)

# NVM Stride-Based
create_config(
    filename="traffic_stride_64.cfg",
    kind="nvm",
    base_addr=0,
    size=1024,
    stride=64
)
```

---

## API Reference

### `CommonTrafficParams`
```python
CommonTrafficParams(
    block_size=64,      # bytes
    min_period=64,      # cycles
    max_period=128,     # cycles
    hot_ratio=0.2,      # 0.0-1.0
    prob_hot=0.8,       # 0.0-1.0
    prob_cold=0.2       # 0.0-1.0
)
```

### `YCSBGenerator`
```python
YCSBGenerator(
    filename,           # str: output file path
    base_addr,          # int: starting address
    size,               # int: region size in bytes
    workload_type="A",  # str: A-F
    workload_mode="RANDOM", # str: RANDOM or LINEAR
    common_params=None  # CommonTrafficParams or None
)
```

### `StrideBasedNVMGenerator`
```python
StrideBasedNVMGenerator(
    filename,           # str: output file path
    base_addr,          # int: starting address
    size,               # int: region size in bytes
    stride=64,          # int: stride in bytes
    common_params=None  # CommonTrafficParams or None
)
```

### `create_config()` (Factory)
```python
create_config(
    filename,           # str: output file path
    kind="ycsb",        # str: "ycsb" or "nvm"
    common_params=None, # CommonTrafficParams or None
    **kwargs            # Type-specific arguments
)
# Returns: filename after generation
```

---

## DRY Principle Benefits

### Before
- `block_size = 64` appeared in both YCSBGenerator and NVMGenerator
- `min_period = 64` and `max_period = 128` duplicated
- Changing timing parameters required updates in 2 places

### After
- All common parameters centralized in `CommonTrafficParams`
- Single point of change for shared configuration
- Easy to create parameter presets (e.g., "aggressive", "conservative")
- Subclasses focus on their unique concerns (workload type vs. stride)

### Example: Creating Custom Presets
```python
# Aggressive profile (lower latency, faster access)
aggressive_params = CommonTrafficParams(
    block_size=128,
    min_period=32,
    max_period=64,
    hot_ratio=0.3,
    prob_hot=0.9
)

# Conservative profile (higher latency, more spacing)
conservative_params = CommonTrafficParams(
    block_size=64,
    min_period=128,
    max_period=256,
    hot_ratio=0.1,
    prob_hot=0.6
)

# Easy to reuse across multiple generators
nvm_gen = StrideBasedNVMGenerator(
    "traffic_stride_64.cfg",
    0, 1024, stride=64,
    common_params=aggressive_params
)
```

---

## Testing the Refactored Code

### Run Individual Tests
```bash
# YCSB Workload A with Random Access
cd /home/ben/Documents/io-exp/gem5/myTest
python3 run_cxl_test.py \
    --kind=ycsb \
    --workload=A \
    --workload_mode=RANDOM \
    --lat=0 \
    --save-dir=./test_ycsb_a_random

# NVM with Stride=64
python3 run_cxl_test.py \
    --kind=nvm \
    --stride=64 \
    --lat=256 \
    --save-dir=./test_nvm_stride64

# NVM with Stride=4096
python3 run_cxl_test.py \
    --kind=nvm \
    --stride=4096 \
    --lat=0 \
    --save-dir=./test_nvm_stride4096
```

### Run Batch Tests
```bash
python3 auto_script.py
```

The script will:
1. Execute all tests defined in the `TESTS` list
2. Create directories with stride-based names (e.g., `m5out_v2_nvm_stride64_0/`)
3. Generate appropriately named config files (e.g., `traffic_stride_64.cfg`)
4. Display a summary table with pass/fail status

---

## Summary of Changes

| Aspect | Before | After |
|--------|--------|-------|
| **NVM Classification** | Random/Linear (coarse) | Stride-based (precise) |
| **Base Parameters** | Duplicated in subclasses | `CommonTrafficParams` class |
| **Config File Naming** | `traffic_random.cfg` | `traffic_stride_64.cfg` |
| **Directory Naming** | Unclear intent | Clear stride values in name |
| **YCSB Support** | Present | Maintained with improved docs |
| **Code Maintainability** | Multiple sources of truth | Single source of truth (DRY) |

---

## Future Extensibility

### Adding New Stride Values
```python
# In auto_script.py TESTS:
{"lat": "0", "kind": "nvm", "host_dram_size": "8GiB", "stride": 8192},  # New!
```
No code changes needed—just add to TESTS.

### Adding New YCSB Workload Types
```python
# In YCSBGenerator._get_read_percent():
mapping = {
    "A": 50,   # 50R/50W
    "B": 95,   # 95R/5W
    "C": 100,  # 100R (NEW)
    "D": 95,   # 95R/5W, insert-heavy (NEW)
}
```

### Extending CommonTrafficParams
```python
class CommonTrafficParams:
    def __init__(self, ..., new_param=default_value):
        # Add new parameter
        self.new_param = new_param
    
    # Subclasses automatically inherit the new parameter
```

---

## Questions?

Refer to inline documentation in:
- [gen_workload.py](gen_workload.py) - Core classes and factory
- [run_cxl_test.py](run_cxl_test.py) - Entry point with argument handling
- [auto_script.py](auto_script.py) - Batch test runner
