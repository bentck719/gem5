# Quick Reference Card - Refactored Traffic Generator

## One-Pagers for Common Tasks

### 1. Generate NVM Config with Default Parameters

```python
from gen_workload import create_config

create_config(
    filename="traffic_stride_64.cfg",
    kind="nvm",
    base_addr=0,
    size=20*1024**3,  # 20 GiB
    stride=64
)
```

**Result:**
- File: `traffic_stride_64.cfg`
- Stride: 64 bytes (cache-line aligned)
- Ready for gem5 TrafficGen

---

### 2. Generate NVM Config with Custom Parameters

```python
from gen_workload import create_config, CommonTrafficParams

params = CommonTrafficParams(
    block_size=128,
    min_period=32,
    max_period=64,
    hot_ratio=0.2
)

create_config(
    filename="traffic_stride_4096.cfg",
    kind="nvm",
    base_addr=0,
    size=20*1024**3,
    stride=4096,
    common_params=params
)
```

---

### 3. Run Single Simulation Test

```bash
cd /home/ben/Documents/io-exp/gem5/myTest

# Test with stride=64, latency=0
python3 run_cxl_test.py \
    --kind=nvm \
    --stride=64 \
    --lat=0 \
    --host_dram_size=8GiB \
    --save-dir=./m5out_v2_nvm_stride64_0
```

---

### 4. Run All Batch Tests

```bash
cd /home/ben/Documents/io-exp/gem5/myTest
python3 auto_script.py
```

**Runs all tests defined in TESTS array:**
- Stride 64, 0ns latency
- Stride 64, 256ns latency
- Stride 4096, 0ns latency
- Stride 4096, 256ns latency

---

## File Naming Quick Map

| Purpose | Pattern | Example |
|---------|---------|---------|
| **NVM Config** | `traffic_stride_<N>.cfg` | `traffic_stride_64.cfg` |
| **YCSB Config** | `traffic_workload_<T>_<M>.cfg` | `traffic_workload_A_random.cfg` |
| **NVM Test Dir** | `m5out_v<V>_nvm_stride<N>_<L>` | `m5out_v2_nvm_stride64_0` |
| **YCSB Test Dir** | `m5out_v<V>_ycsb_w<T>_<M>_<L>` | `m5out_v2_ycsb_wA_random_0` |

**Abbreviations:** V=version, N=stride, L=latency, T=workload type, M=mode

---

## Class Hierarchy at a Glance

```
CommonTrafficParams ◄─────────────────────┐
    ├─ block_size (default: 64)           │ Used by:
    ├─ min_period (default: 64)           │ ├─ YCSBGenerator
    ├─ max_period (default: 128)          │ ├─ StrideBasedNVMGenerator
    ├─ hot_ratio (default: 0.2)           │ └─ Future generators
    ├─ prob_hot (default: 0.8)            │
    └─ prob_cold (default: 0.2)           │
                                          │
TrafficGenConfig (Abstract)               │
    ├─ _write_header()                    │
    ├─ _write_transitions()               │
    ├─ _calculate_regions()               │
    └─ generate()                         │
         │                                 │
         ├──────────────────────────────────┘
         │
         ├─ YCSBGenerator (Workload types A, B, ...)
         └─ StrideBasedNVMGenerator (Stride values)

Factory: create_config(kind, ...)
```

---

## Stride Value Reference

```
64      → Cache-line access (standard)
128     → 2× cache-line (variants)
256     → L3 cache aligned
4096    → Page-aligned (common)
8192    → Huge page (2M)
16384   → Multi-page
```

---

## Command-Line Arguments

### For NVM Tests
```
--kind nvm              (required)
--stride <N>            (default: 64, options: 64, 128, 256, 4096, ...)
--lat <N>               (default: 256, options: 0, 256, etc.)
--host_dram_size <X>    (default: 6GiB, options: 6GiB, 8GiB, etc.)
--save-dir <PATH>       (default: ., output directory)
```

### For YCSB Tests
```
--kind ycsb             (required)
--workload <T>          (default: A, options: A, B, C, ...)
--workload_mode <M>     (default: RANDOM, options: RANDOM, LINEAR)
--lat <N>               (default: 256)
--host_dram_size <X>    (default: 6GiB)
--save-dir <PATH>       (default: .)
```

---

## File Output Structure

After running `python3 run_cxl_test.py --kind=nvm --stride=64 --lat=0`:

```
m5out_v2_nvm_stride64_0/
├── traffic_stride_64.cfg          ← Generated config file
├── simulation_config.json         ← Test parameters (JSON)
├── gem5.log                       ← Execution log
├── stats.txt                      ← Performance statistics
└── m5out/
    ├── system.terminal
    └── stats.txt
```

---

## CommonTrafficParams Presets

```python
from gen_workload import CommonTrafficParams

# Preset 1: Baseline (defaults)
baseline = CommonTrafficParams()

# Preset 2: Conservative (low stress)
conservative = CommonTrafficParams(
    min_period=256,
    max_period=512,
    hot_ratio=0.05,
    prob_hot=0.5
)

# Preset 3: Aggressive (high stress)
aggressive = CommonTrafficParams(
    block_size=256,
    min_period=16,
    max_period=32,
    hot_ratio=0.5,
    prob_hot=0.95
)

# Preset 4: Memory-intensive (all hot)
memory_intensive = CommonTrafficParams(
    hot_ratio=1.0,
    prob_hot=1.0,
    prob_cold=0.0
)

# Usage: Pass to any generator
gen = StrideBasedNVMGenerator(
    "config.cfg", 0, 1024, stride=64,
    common_params=aggressive
)
```

---

## Error Handling Examples

### ValueError: Invalid Stride
```python
# ❌ Will raise ValueError
create_config(
    filename="bad.cfg",
    kind="nvm",
    stride="invalid"  # Should be int
)

# ✅ Correct
create_config(
    filename="good.cfg",
    kind="nvm",
    stride=64  # int
)
```

### File Not Found
```python
import os

# ✅ Ensure directory exists
os.makedirs("./output_dir", exist_ok=True)

cfg_file = os.path.join("./output_dir", "config.cfg")
create_config(
    filename=cfg_file,
    kind="nvm",
    stride=64
)
```

---

## Performance Tuning

### For Latency-Sensitive Tests
```python
params = CommonTrafficParams(
    min_period=16,          # Short periods
    max_period=32,
    block_size=256,         # Large blocks
    hot_ratio=0.3
)
```

### For Throughput Tests
```python
params = CommonTrafficParams(
    min_period=64,          # Normal periods
    max_period=128,
    block_size=128,         # Medium blocks
    hot_ratio=0.2
)
```

### For Bandwidth Tests
```python
params = CommonTrafficParams(
    min_period=8,           # Very short periods
    max_period=16,
    block_size=512,         # Large blocks
    hot_ratio=0.5           # Half of memory
)
```

---

## Migration from Old Code

### Old NVMGenerator
```python
# ❌ OLD (doesn't work anymore)
from gen_workload import NVMGenerator
gen = NVMGenerator("config.cfg", 0, 1024, stride=64)
```

### New StrideBasedNVMGenerator
```python
# ✅ NEW (use this)
from gen_workload import StrideBasedNVMGenerator
gen = StrideBasedNVMGenerator("traffic_stride_64.cfg", 0, 1024, stride=64)
```

### Using Factory (Recommended)
```python
# ✅ BEST (uses factory)
from gen_workload import create_config
create_config(
    filename="traffic_stride_64.cfg",
    kind="nvm",
    base_addr=0,
    size=1024,
    stride=64
)
```

---

## Testing Your Setup

### Verify Installation
```bash
cd /home/ben/Documents/io-exp/gem5/myTest

# Test 1: Import modules
python3 -c "from gen_workload import create_config, CommonTrafficParams; print('✓ Imports OK')"

# Test 2: Generate config
python3 -c "
from gen_workload import create_config
create_config('test.cfg', kind='nvm', base_addr=0, size=1024, stride=64)
import os
print(f'✓ Config generated: {os.path.exists(\"test.cfg\")}')
"

# Test 3: Check output
cat test.cfg
```

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| **`ModuleNotFoundError`** | Ensure working directory is `/home/ben/Documents/io-exp/gem5/myTest` |
| **`FileNotFoundError` (output)** | Create directory: `mkdir -p output_dir` |
| **Wrong stride in config** | Check `--stride` argument matches desired value |
| **Config not generated** | Check file permissions and disk space |
| **Simulation fails** | Review `gem5.log` in output directory |

---

## Useful Commands

```bash
# List all test directories
ls -d m5out_v2_nvm_stride*/

# View config file
cat m5out_v2_nvm_stride64_0/traffic_stride_64.cfg

# View test metadata
cat m5out_v2_nvm_stride64_0/simulation_config.json | python3 -m json.tool

# Compare two configs
diff m5out_v2_nvm_stride64_0/traffic_stride_64.cfg \
     m5out_v2_nvm_stride4096_0/traffic_stride_4096.cfg

# Find all stats files
find . -name "stats.txt" | head -5

# Check total output size
du -sh m5out_v2_nvm_stride*/
```

---

## Documentation Files

| File | Purpose | Audience |
|------|---------|----------|
| [gen_workload.py](gen_workload.py) | Core implementation | Developers |
| [run_cxl_test.py](run_cxl_test.py) | Entry point | Users running tests |
| [auto_script.py](auto_script.py) | Batch runner | Users running batches |
| [REFACTORING_GUIDE.md](REFACTORING_GUIDE.md) | Architecture docs | Maintainers |
| [FOLDER_STRUCTURE.md](FOLDER_STRUCTURE.md) | Naming conventions | All |
| [CODE_EXAMPLES.md](CODE_EXAMPLES.md) | Usage examples | Developers |
| [REFACTORING_SUMMARY.md](REFACTORING_SUMMARY.md) | Project summary | Project leads |
| This file | Quick reference | Everyone |

---

## Key Takeaways

✅ **Stride-based NVM classification** replaces Random/Linear  
✅ **CommonTrafficParams** eliminates parameter duplication  
✅ **Clear naming conventions** for configs and directories  
✅ **Factory function** simplifies config creation  
✅ **Full backward compatibility** with YCSB workloads  
✅ **Comprehensive documentation** for all use cases  

**Ready to use!** Start with Example 1 above.

---

Last updated: January 4, 2026
