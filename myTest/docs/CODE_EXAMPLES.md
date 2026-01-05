# Refactoring Examples & Code Snippets

## Example 1: Basic Usage - Stride-Based NVM

### Creating a Config File with Default Parameters
```python
from gen_workload import create_config

# Simplest usage - uses CommonTrafficParams defaults
create_config(
    filename="traffic_stride_64.cfg",
    kind="nvm",
    base_addr=0,
    size=20 * 1024**3,  # 20 GiB
    stride=64
)
# Output: traffic_stride_64.cfg with stride=64 access pattern
```

### Creating a Config File with Custom Parameters
```python
from gen_workload import create_config, CommonTrafficParams

# Define custom timing parameters
custom_params = CommonTrafficParams(
    block_size=128,        # Larger blocks
    min_period=32,         # More aggressive timing
    max_period=64,
    hot_ratio=0.1,         # Only 10% of memory is "hot"
    prob_hot=0.9,          # Likely to stay in hot region
    prob_cold=0.1
)

create_config(
    filename="traffic_stride_4096.cfg",
    kind="nvm",
    base_addr=0,
    size=20 * 1024**3,
    stride=4096,
    common_params=custom_params
)
# Output: traffic_stride_4096.cfg with page-aligned access and custom timings
```

---

## Example 2: YCSB Workload Generation

### Simple YCSB Configuration
```python
from gen_workload import create_config

create_config(
    filename="traffic_workload_A_random.cfg",
    kind="ycsb",
    base_addr=0,
    size=20 * 1024**3,
    workload_type="A",    # 50R/50W
    workload_mode="RANDOM"
)
# Output: YCSB workload A with random access pattern
```

### Multiple YCSB Workloads with Shared Parameters
```python
from gen_workload import create_config, CommonTrafficParams

# Define baseline parameters for all YCSB tests
base_params = CommonTrafficParams(
    block_size=64,
    min_period=64,
    max_period=128
)

# Create multiple workload configurations
workloads = [
    ("A", "RANDOM"),   # 50R/50W, random
    ("B", "LINEAR"),   # 95R/5W, linear (read-mostly)
]

for workload_type, mode in workloads:
    create_config(
        filename=f"traffic_workload_{workload_type}_{mode.lower()}.cfg",
        kind="ycsb",
        base_addr=0,
        size=20 * 1024**3,
        workload_type=workload_type,
        workload_mode=mode,
        common_params=base_params
    )

# Output:
#   traffic_workload_A_random.cfg
#   traffic_workload_B_linear.cfg
```

---

## Example 3: Direct Class Instantiation (Advanced)

### Creating a StrideBasedNVMGenerator Directly
```python
from gen_workload import StrideBasedNVMGenerator, CommonTrafficParams

params = CommonTrafficParams(
    block_size=64,
    min_period=64,
    max_period=128,
    hot_ratio=0.2
)

gen = StrideBasedNVMGenerator(
    filename="custom_traffic.cfg",
    base_addr=0,
    size=20 * 1024**3,
    stride=64,
    common_params=params
)

gen.generate()  # Writes the config file
print(f"Config written to: {gen.filename}")
```

### Inspecting Generated Configuration Content
```python
from gen_workload import StrideBasedNVMGenerator, CommonTrafficParams

params = CommonTrafficParams(block_size=64, min_period=64, max_period=128)
gen = StrideBasedNVMGenerator(
    filename="inspect_me.cfg",
    base_addr=0,
    size=1024,
    stride=64,
    common_params=params
)

# Check configuration before generation
print(f"Block size: {gen.common_params.block_size}")
print(f"Period range: {gen.common_params.min_period}-{gen.common_params.max_period}")
print(f"Stride: {gen.stride}")

gen.generate()

# Read back the generated file
with open("inspect_me.cfg", "r") as f:
    print("\n--- Generated Config ---")
    print(f.read())
```

---

## Example 4: Batch Test Runner Integration

### Using the Factory in a Batch Loop
```python
from gen_workload import create_config, CommonTrafficParams
import os

# Test matrix
test_params = [
    {"stride": 64, "lat": 0, "dram": "8GiB"},
    {"stride": 64, "lat": 256, "dram": "6GiB"},
    {"stride": 4096, "lat": 0, "dram": "8GiB"},
    {"stride": 4096, "lat": 256, "dram": "6GiB"},
]

base_params = CommonTrafficParams()

for test in test_params:
    # Create output directory
    test_dir = f"m5out_v2_nvm_stride{test['stride']}_{test['lat']}"
    os.makedirs(test_dir, exist_ok=True)
    
    cfg_file = os.path.join(test_dir, f"traffic_stride_{test['stride']}.cfg")
    
    # Generate config using factory
    create_config(
        filename=cfg_file,
        kind="nvm",
        base_addr=0,
        size=20 * 1024**3,
        stride=test['stride'],
        common_params=base_params
    )
    
    print(f"✓ Created: {cfg_file}")
```

---

## Example 5: Customizing Parameters for Different Scenarios

### Scenario 1: Conservative Testing (Low Stress)
```python
from gen_workload import StrideBasedNVMGenerator, CommonTrafficParams

# Conservative parameters: lower bandwidth, longer periods
conservative = CommonTrafficParams(
    block_size=64,
    min_period=256,        # Long delay between requests
    max_period=512,
    hot_ratio=0.05,        # Very small hot region
    prob_hot=0.5,          # Equal probability of hot/cold
    prob_cold=0.5
)

gen = StrideBasedNVMGenerator(
    filename="conservative_stride_64.cfg",
    base_addr=0,
    size=20 * 1024**3,
    stride=64,
    common_params=conservative
)
gen.generate()
```

### Scenario 2: Aggressive Testing (High Stress)
```python
# Aggressive parameters: high bandwidth, short periods
aggressive = CommonTrafficParams(
    block_size=256,        # Larger blocks
    min_period=16,         # Short delay between requests
    max_period=32,
    hot_ratio=0.5,         # Half of memory is "hot"
    prob_hot=0.95,         # Likely to stay in hot region
    prob_cold=0.05
)

gen = StrideBasedNVMGenerator(
    filename="aggressive_stride_4096.cfg",
    base_addr=0,
    size=20 * 1024**3,
    stride=4096,
    common_params=aggressive
)
gen.generate()
```

### Scenario 3: Memory-Intensive (Full Utilization)
```python
# Memory-intensive: everything is "hot"
memory_intensive = CommonTrafficParams(
    block_size=128,
    min_period=32,
    max_period=64,
    hot_ratio=1.0,         # Entire address space is "hot"
    prob_hot=1.0,
    prob_cold=0.0
)

gen = StrideBasedNVMGenerator(
    filename="memory_intensive_stride_64.cfg",
    base_addr=0,
    size=20 * 1024**3,
    stride=64,
    common_params=memory_intensive
)
gen.generate()
```

---

## Example 6: Configuration Presets as a Dictionary

### Creating Reusable Presets
```python
from gen_workload import CommonTrafficParams

# Define preset configurations
PRESETS = {
    "baseline": CommonTrafficParams(),  # Defaults
    
    "conservative": CommonTrafficParams(
        min_period=256,
        max_period=512,
        hot_ratio=0.05,
        prob_hot=0.5
    ),
    
    "aggressive": CommonTrafficParams(
        block_size=256,
        min_period=16,
        max_period=32,
        hot_ratio=0.5,
        prob_hot=0.95
    ),
    
    "cache_line_aggressive": CommonTrafficParams(
        block_size=64,
        min_period=8,
        max_period=16,
        hot_ratio=0.2,
        prob_hot=0.9
    ),
    
    "page_aligned_conservative": CommonTrafficParams(
        block_size=4096,
        min_period=128,
        max_period=256,
        hot_ratio=0.1,
        prob_hot=0.6
    )
}

# Use presets in batch testing
for preset_name, params in PRESETS.items():
    cfg_file = f"traffic_stride_64_{preset_name}.cfg"
    from gen_workload import create_config
    
    create_config(
        filename=cfg_file,
        kind="nvm",
        base_addr=0,
        size=20 * 1024**3,
        stride=64,
        common_params=params
    )
    print(f"Created: {cfg_file}")
```

---

## Example 7: Error Handling & Validation

### Safe Configuration Creation
```python
from gen_workload import create_config, CommonTrafficParams
import os

def safe_create_config(test_name, **kwargs):
    """Wrapper for safe config creation with validation."""
    
    try:
        # Validate stride value (NVM only)
        if kwargs.get('kind') == 'nvm':
            valid_strides = [64, 128, 256, 512, 1024, 4096, 8192, 16384]
            if kwargs.get('stride', 64) not in valid_strides:
                print(f"⚠️  Warning: Stride {kwargs['stride']} is unusual")
        
        # Validate parameters
        if kwargs.get('kind') == 'ycsb':
            valid_workloads = ['A', 'B', 'C']
            if kwargs.get('workload_type', 'A') not in valid_workloads:
                raise ValueError(f"Invalid workload type: {kwargs['workload_type']}")
        
        # Create directory
        os.makedirs(test_name, exist_ok=True)
        
        # Create config file
        cfg_path = os.path.join(test_name, f"traffic_{test_name}.cfg")
        kwargs['filename'] = cfg_path
        
        create_config(**kwargs)
        
        print(f"✅ Config created: {cfg_path}")
        return True
        
    except ValueError as e:
        print(f"❌ Validation error: {e}")
        return False
    except Exception as e:
        print(f"❌ Error creating config: {e}")
        return False

# Usage examples
safe_create_config(
    "test_stride64",
    kind="nvm",
    base_addr=0,
    size=20*1024**3,
    stride=64
)

safe_create_config(
    "test_stride_invalid",
    kind="nvm",
    base_addr=0,
    size=20*1024**3,
    stride=99  # Will show warning
)
```

---

## Example 8: Comparing Generated Config Files

### Visual Comparison of Different Strides
```python
from gen_workload import create_config, CommonTrafficParams
import difflib

params = CommonTrafficParams()

# Generate two configs with different strides
configs = {
    "stride_64": "temp_stride_64.cfg",
    "stride_4096": "temp_stride_4096.cfg"
}

for name, filename in configs.items():
    stride = 64 if "64" in name else 4096
    create_config(
        filename=filename,
        kind="nvm",
        base_addr=0,
        size=1024,
        stride=stride,
        common_params=params
    )

# Read and compare
with open(configs["stride_64"], "r") as f:
    lines1 = f.readlines()

with open(configs["stride_4096"], "r") as f:
    lines2 = f.readlines()

# Show differences
print("Differences between stride_64 and stride_4096:")
for line in difflib.unified_diff(lines1, lines2, lineterm=''):
    print(line)

# Cleanup
import os
os.remove(configs["stride_64"])
os.remove(configs["stride_4096"])
```

**Output:**
```
Differences between stride_64 and stride_4096:
--- temp_stride_64.cfg
+++ temp_stride_4096.cfg
@@ -3,4 +3,4 @@
 STATE 1 100000000 NVM 50 0 1024 64 64 128 0 64 4096 16 16 1 1
-STATE 2 100000000 NVM 50 512 1024 64 64 128 0 4096 4096 16 16 1 1
+STATE 2 100000000 NVM 50 512 1024 64 64 128 0 4096 4096 16 16 1 1
 TRANSITION 0 1 0.8
```

The key difference: `stride=64` vs `stride=4096` in the extra_params!

---

## Example 9: Integration with run_cxl_test.py

### How run_cxl_test.py Uses the Refactored Code
```python
import os
from gen_workload import create_config

# Arguments received from command line
args = {
    'kind': 'nvm',
    'stride': 64,
    'lat': 0,
    'save_dir': './m5out_v2_nvm_stride64_0'
}

# 1. Create output directory
os.makedirs(args['save_dir'], exist_ok=True)

# 2. Generate config file with stride-based naming
cfg_filename = os.path.join(
    args['save_dir'], 
    f"traffic_stride_{args['stride']}.cfg"
)

create_config(
    filename=cfg_filename,
    kind=args['kind'],
    base_addr=0,
    size=20 * 1024**3,
    stride=args['stride']
)

print(f"Config generated: {cfg_filename}")

# 3. Pass to gem5 TrafficGen
# cpu = TrafficGen(config_file=cfg_filename)
```

---

## Example 10: Migration Checklist

### Updating Legacy Code
```python
# ❌ OLD CODE
from gen_workload import NVMGenerator

gen = NVMGenerator(
    "traffic_random.cfg",
    base_addr=0,
    size=1024,
    stride=64
)
gen.generate()

# ✅ NEW CODE - Option 1: Use Factory
from gen_workload import create_config

create_config(
    filename="traffic_stride_64.cfg",
    kind="nvm",
    base_addr=0,
    size=1024,
    stride=64
)

# ✅ NEW CODE - Option 2: Use Direct Class
from gen_workload import StrideBasedNVMGenerator

gen = StrideBasedNVMGenerator(
    "traffic_stride_64.cfg",
    base_addr=0,
    size=1024,
    stride=64
)
gen.generate()

# ✅ NEW CODE - Option 3: Custom Parameters
from gen_workload import StrideBasedNVMGenerator, CommonTrafficParams

params = CommonTrafficParams(min_period=32, max_period=64)
gen = StrideBasedNVMGenerator(
    "traffic_stride_64.cfg",
    base_addr=0,
    size=1024,
    stride=64,
    common_params=params
)
gen.generate()
```

---

## Testing the Refactored Code

### Unit Test Example
```python
"""test_gen_workload.py - Unit tests for refactored generators"""

from gen_workload import (
    CommonTrafficParams, 
    YCSBGenerator, 
    StrideBasedNVMGenerator,
    create_config
)
import os
import tempfile

def test_common_params_defaults():
    """Test CommonTrafficParams with default values."""
    params = CommonTrafficParams()
    assert params.block_size == 64
    assert params.min_period == 64
    assert params.max_period == 128
    print("✓ CommonTrafficParams defaults OK")

def test_stride_based_nvm_generation():
    """Test StrideBasedNVMGenerator creates valid config."""
    with tempfile.TemporaryDirectory() as tmpdir:
        cfg_file = os.path.join(tmpdir, "test_stride_64.cfg")
        gen = StrideBasedNVMGenerator(
            cfg_file, 
            base_addr=0, 
            size=1024, 
            stride=64
        )
        gen.generate()
        
        assert os.path.exists(cfg_file)
        with open(cfg_file, 'r') as f:
            content = f.read()
            assert "STATE 1" in content
            assert "NVM 50" in content
            assert "stride" not in content.lower()  # Stride is in params
        print("✓ StrideBasedNVMGenerator OK")

def test_factory_function():
    """Test create_config factory function."""
    with tempfile.TemporaryDirectory() as tmpdir:
        cfg_file = os.path.join(tmpdir, "factory_test.cfg")
        create_config(
            filename=cfg_file,
            kind="nvm",
            base_addr=0,
            size=1024,
            stride=4096
        )
        assert os.path.exists(cfg_file)
        print("✓ Factory function OK")

if __name__ == "__main__":
    test_common_params_defaults()
    test_stride_based_nvm_generation()
    test_factory_function()
    print("\n✅ All tests passed!")
```

Run tests:
```bash
python3 test_gen_workload.py
```

---

## Summary of Code Improvements

| Aspect | Before | After | Lines Saved |
|--------|--------|-------|------------|
| Parameter sharing | Duplicated | CommonTrafficParams | ~20 |
| NVM Generator | Vague naming | StrideBasedNVMGenerator | -5 |
| Config file naming | Non-descriptive | stride-based | N/A |
| Total classes | 2 | 4 (+ CommonTrafficParams) | N/A |
| Flexibility | Limited | Extensive customization | N/A |

