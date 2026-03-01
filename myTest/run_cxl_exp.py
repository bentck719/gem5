import m5
from m5.objects import *
import argparse
import json
import os # used for path handling

parser = argparse.ArgumentParser(description="CXL SSD memory tester")
parser.add_argument("--workload", default="A", help="YCSB Workload type (A-F)")
parser.add_argument("--workload_mode", default="LINEAR", help="YCSB workload mode (RANDOM, LINEAR)")
parser.add_argument("--kind", default="ycsb", choices=["ycsb", "nvm"], 
                    help="Generator kind: 'ycsb' for YCSB workloads or 'nvm' for stride-based NVM workloads")
parser.add_argument("--stride", type=int, default=64, help="Stride size for NVM generator (e.g., 64, 128, 4096)")
parser.add_argument("--size", type=int, default=2, help="Size of the memory region in GiB")
parser.add_argument("--host_dram_size", type=str, default='6GiB', help="Size of the host DRAM")
parser.add_argument("--lat", type=int, default=256, help="CXL large access threshold")
parser.add_argument("--threshold_distributed", type=int, default=4, help="Threshold for distributed access")
parser.add_argument("--save-dir", default=".", help="Directory to save logs")

args = parser.parse_args()

# Parameter Setup
size_bytes = args.size * 1024**3
base_addr = 0

# Generate descriptive config filename based on workload type and parameters
if args.kind == "ycsb":
    # YCSB: traffic_workload_<type>_<mode>.cfg
    cfg_filename = os.path.join(args.save_dir, 
                                f"traffic_workload_{args.workload}_{args.workload_mode.lower()}.cfg")
elif args.kind == "nvm":
    # NVM (stride-based): traffic_stride_<stride_value>.cfg
    cfg_filename = os.path.join(args.save_dir, 
                                f"traffic_stride_{args.stride}.cfg")

# 1. Generate the Config File using the refactored factory function
# create_config(
#     filename=cfg_filename,
#     kind=args.kind,
#     base_addr=base_addr,
#     size=size_bytes,
#     stride=args.stride,                # Used only for NVM generator
#     workload_type=args.workload,       # Used only for YCSB generator
#     workload_mode=args.workload_mode   # Used by YCSB generator
# )

# 2. Save Simulation Metadata
os.makedirs(args.save_dir, exist_ok=True)
with open(os.path.join(args.save_dir, "simulation_config.json"), "w") as f:
    json.dump(vars(args), f, indent=2)

# 3. Gem5 System Construction
system = System()
system.mem_mode = "timing"
system.clk_domain = SrcClockDomain(clock='1GHz', voltage_domain=VoltageDomain())

system.cache_line_size = 1024

# Using the generated config file
cpu = TrafficGen(config_file=cfg_filename)
system.cpu = cpu

system.membus = SystemXBar(max_routing_table_size=(1<<15))
system.mem_ranges = [AddrRange(base_addr, base_addr + size_bytes)]

system.cxl_ssd = CxlSSD(
    range = AddrRange(base_addr, base_addr + size_bytes),
    latency = "80ns",
    bandwidth = "32GiB/s",
    cxl_bandwidth = "32GiB/s",
    host_dram_size = args.host_dram_size,
    ssd_latency = "100us",
    cxl_large_access_threshold = args.lat,
    threshold_distributed = args.threshold_distributed

)

system.cpu.port = system.membus.cpu_side_ports
system.cxl_ssd.port = system.membus.mem_side_ports

root = Root(full_system=False, system=system)

print(f"--- Simulation Started ---")
print(f"Config: {cfg_filename}")
print(f"Range: {hex(base_addr)} - {hex(base_addr + size_bytes)}")

m5.instantiate()
exit_event = m5.simulate(10_000_000_000)

if exit_event.getCause() != "simulate() limit reached":
    print(f"Simulation exited due to: {exit_event.getCause()}")
    exit(1)