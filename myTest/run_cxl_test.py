import m5
from m5.objects import *
from gen_workload import create_ycsb_cfg
import argparse
import json

parser = argparse.ArgumentParser(description="CXL SSD memory tester")
parser.add_argument("--workload", default="A", help="YCSB Workload type (A-F)")
parser.add_argument("--workload_mode", default="RANDOM", help="YCSB Workload mode (e.g., RANDOM, LINEAR)")
parser.add_argument("--size", type=int, default=20, help="Size of the memory region in GiB")
parser.add_argument("--host_dram_size", type=str, default='6GiB', help="Size of the host DRAM in the CXL SSD")
parser.add_argument("--lat", type=int, default=256, help="CXL large access threshold in bytes")
parser.add_argument("--save-dir", default=".", help="Directory to save simulation logs")
args = parser.parse_args()

workload = args.workload
workload_mode = args.workload_mode
base_addr = 0
host_dram_size = args.host_dram_size
size = args.size * 1024**3
lat = args.lat

log_filename = f"{args.save_dir}/simulation_config.log"
with open(log_filename, "w") as f:
    json.dump(vars(args), f, indent=2)

cfg_filename = f"nvm_{workload_mode.lower()}.cfg"
print(cfg_filename)
# create_ycsb_cfg(cfg_filename, workload_type=workload, workload_mode=workload_mode, base_addr=base_addr, size=size)

system = System()
system.mem_mode = "timing"
system.clk_domain=SrcClockDomain(clock='1GHz', voltage_domain=VoltageDomain())

cpu = TrafficGen(config_file=cfg_filename)
system.cpu = cpu

system.membus = SystemXBar(
    max_routing_table_size=(1<<15)
)
system.mem_ranges=[AddrRange(base_addr, base_addr+size)]

system.cxl_ssd=CxlSSD(
    range = AddrRange(base_addr, base_addr+size),
    latency = "80ns",
    bandwidth = "32GiB/s",
    cxl_bandwidth = "32GiB/s",
    host_dram_size = host_dram_size,
    ssd_latency = "100us",
    cxl_large_access_threshold = lat
)

system.cpu.port = system.membus.cpu_side_ports

# system.membus.seriallink = SerialLink(
#     ranges=system.mem_ranges,
#     req_size=16,
#     resp_size=16,
#     num_lanes=16,
#     link_speed=32 # PCIe Gen5 32GT/s
# )

# sl = system.membus.seriallink
# system.membus.mem_side_ports = sl.cpu_side_port
# sl.mem_side_port = system.cxl_ssd.port

system.cxl_ssd.port = system.membus.mem_side_ports

root = Root(full_system=False, system=system)

print(f"--- Simulation Started ---")
print(f"Using config: {cfg_filename}")
print("System Memory Ranges in Python:", hex(system.mem_ranges[0].start), hex(system.mem_ranges[0].end))

m5.instantiate()
exit_event = m5.simulate(1_000_000_000_000)

if exit_event.getCause() != "simulate() limit reached":
    exit(1)