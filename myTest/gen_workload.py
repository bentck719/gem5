import abc


class CommonTrafficParams:
    """Common traffic generation parameters shared by all workload types.
    
    This class holds base configuration parameters to follow the DRY principle
    and reduce duplication between YCSB and NVM generators.
    """
    
    def __init__(self, block_size=64, min_period=64, max_period=128, 
                 hot_ratio=0.2, prob_hot=0.8, prob_cold=0.2):
        """
        Args:
            block_size: Size of memory access block (bytes)
            min_period: Minimum delay between requests (cycles)
            max_period: Maximum delay between requests (cycles)
            hot_ratio: Ratio of address space considered "hot" (0.0-1.0)
            prob_hot: Probability of transitioning to hot state (0.0-1.0)
            prob_cold: Probability of transitioning to cold state (0.0-1.0)
        """
        self.block_size = block_size
        self.min_period = min_period
        self.max_period = max_period
        self.hot_ratio = hot_ratio
        self.prob_hot = prob_hot
        self.prob_cold = prob_cold


class TrafficGenConfig(abc.ABC):
    """Base class for generating Gem5 TrafficGen config files.
    
    Uses CommonTrafficParams to hold shared configuration parameters,
    while subclasses implement their specific STATE formats.
    """
    
    def __init__(self, filename, base_addr, size, duration="100000000", 
                 common_params=None):
        """
        Args:
            filename: Output config file path
            base_addr: Base address for memory region
            size: Total memory region size (bytes)
            duration: Simulation duration for each state
            common_params: CommonTrafficParams instance (uses defaults if None)
        """
        self.filename = filename
        self.base_addr = base_addr
        self.size = size
        self.duration = duration
        self.common_params = common_params or CommonTrafficParams()

    def _calculate_regions(self):
        """Calculates hot and cold address ranges based on hot_ratio."""
        hot_limit = int(self.size * self.common_params.hot_ratio)
        total_limit = self.size
        
        # (Start, End) tuples
        hot_region = (self.base_addr, self.base_addr + hot_limit)
        cold_region = (self.base_addr + hot_limit, self.base_addr + total_limit)
        return hot_region, cold_region

    def _write_header(self, f):
        """Write the trafficgen initialization header."""
        f.write("INIT 0\n")
        f.write("STATE 0 1000000000 IDLE\n")

    def _write_transitions(self, f):
        """Writes the standard transition logic shared by all workloads."""
        transitions = [
            f"TRANSITION 0 1 {self.common_params.prob_hot}",
            f"TRANSITION 0 2 {self.common_params.prob_cold}",
            f"TRANSITION 1 1 {self.common_params.prob_hot}",
            f"TRANSITION 1 2 {self.common_params.prob_cold}",
            f"TRANSITION 2 1 {self.common_params.prob_hot}",
            f"TRANSITION 2 2 {self.common_params.prob_cold}",
        ]
        f.write("\n".join(transitions) + "\n")

    @abc.abstractmethod
    def _get_state_config(self, region_start, region_end):
        """Subclasses must implement the specific string format for a STATE line."""
        pass

    def generate(self):
        """Main execution method to write the file."""
        print(f"--- Generating Config: {self.filename} ---")
        hot_region, cold_region = self._calculate_regions()

        with open(self.filename, "w") as f:
            self._write_header(f)
            
            # State 1: Hot Region
            state1_cfg = self._get_state_config(*hot_region)
            f.write(f"STATE 1 {self.duration} {state1_cfg}\n")
            
            # State 2: Cold Region
            state2_cfg = self._get_state_config(*cold_region)
            f.write(f"STATE 2 {self.duration} {state2_cfg}\n")
            
            self._write_transitions(f)


class YCSBGenerator(TrafficGenConfig):
    """Generates YCSB-style workloads (Read/Write ratios).
    
    YCSB classification uses workload types (A-F) to define different
    read/write patterns, independent of access patterns.
    """
    
    def __init__(self, filename, base_addr, size, workload_type="A", 
                 workload_mode="RANDOM", common_params=None, **kwargs):
        """
        Args:
            filename: Output config file path
            base_addr: Base address for memory region
            size: Total memory region size (bytes)
            workload_type: YCSB workload type (A=50R50W, B=95R5W, etc.)
            workload_mode: YCSB access pattern (RANDOM, LINEAR)
            common_params: CommonTrafficParams instance
        """
        super().__init__(filename, base_addr, size, common_params=common_params)
        self.workload_mode = workload_mode
        self.read_percent = self._get_read_percent(workload_type)

    @staticmethod
    def _get_read_percent(workload_type):
        """Map YCSB workload types to read percentages."""
        mapping = {"A": 50, "B": 95}
        return mapping.get(workload_type, 100) # Default to 100% read if unknown

    def _get_state_config(self, start, end):
        """Generate YCSB-specific state configuration.
        
        Format: <pattern> <read_percent> <start> <end> <blk_size> <min_period> <max_period> <store_ratio>
        """
        return (f"{self.workload_mode} {self.read_percent} {start} {end} "
                f"{self.common_params.block_size} {self.common_params.min_period} "
                f"{self.common_params.max_period} 0")


class StrideBasedNVMGenerator(TrafficGenConfig):
    """Generates NVM-specific workloads classified by stride value.
    
    Stride-based classification allows parameterization of memory access patterns
    (e.g., stride=64 for cache-line accesses, stride=4096 for page-aligned accesses).
    Replaces the previous Random/Linear classification model.
    """
    
    def __init__(self, filename, base_addr, size, stride=64, 
                 common_params=None, **kwargs):
        """
        Args:
            filename: Output config file path
            base_addr: Base address for memory region
            size: Total memory region size (bytes)
            stride: Access stride size in bytes (e.g., 64, 128, 4096)
            common_params: CommonTrafficParams instance
        """
        super().__init__(filename, base_addr, size, common_params=common_params)
        self.stride = stride
        
        # NVM-specific parameters (derived from common_params and stride)
        # Format: <block_size> <min_period> <max_period> <data_limit> <stride> <page_size> <nbr_of_banks> <nbr_of_banks_util> <_addr_mapping> <nbr_of_ranks>
        self.extra_params = (
            f"{self.common_params.block_size} "
            f"{self.common_params.min_period} "
            f"{self.common_params.max_period} "
            f"0 {self.stride} 4096 16 16 1 1"
        )

    def _get_state_config(self, start, end):
        """Generate stride-based NVM state configuration.
        
        Format: NVM <read_percent> <start> <end> <extra_params...>
        Uses 50% read percentage for balanced NVM workloads.
        """
        return f"NVM 50 {start} {end} {self.extra_params}"

# --- Factory Function for Backward Compatibility & New Stride-Based Logic ---
def create_config(filename, kind="ycsb", common_params=None, **kwargs):
    """
    Factory function to create the appropriate traffic generator.
    
    This function maintains backward compatibility while supporting the new
    stride-based NVM classification system.
    
    Args:
        filename: Output config file path
        kind: Generator type - 'ycsb' or 'nvm' (default: 'ycsb')
        common_params: CommonTrafficParams instance (uses defaults if None)
        **kwargs: Type-specific arguments:
            - YCSB: base_addr, size, workload_type, workload_mode
            - NVM: base_addr, size, stride
    
    Returns:
        The generated config filename after calling generate()
    
    Raises:
        ValueError: If kind is not recognized
    """
    kind = kind.lower()
    
    if common_params is None:
        common_params = CommonTrafficParams()
    
    if kind == "ycsb":
        # Extract YCSB-specific arguments
        gen = YCSBGenerator(
            filename, 
            kwargs.get('base_addr', 0), 
            kwargs.get('size', 1024),
            workload_type=kwargs.get('workload_type', 'A'),
            workload_mode=kwargs.get('workload_mode', 'RANDOM'),
            common_params=common_params
        )
    elif kind == "nvm":
        # Extract NVM-specific arguments (stride-based)
        gen = StrideBasedNVMGenerator(
            filename,
            kwargs.get('base_addr', 0),
            kwargs.get('size', 1024),
            stride=kwargs.get('stride', 64),
            common_params=common_params
        )
    else:
        raise ValueError(f"Unknown config kind: {kind}. Must be 'ycsb' or 'nvm'.")
    
    gen.generate()
    return filename