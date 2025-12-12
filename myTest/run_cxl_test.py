import m5
from m5.objects import *
from gen_ycsb_trace import create_ycsb_cfg
import argparse

parser = argparse.ArgumentParser(description="CXL SSD memory tester")
parser.add_argument("--workload", default="A", help="YCSB Workload type (A-F)")
parser.add_argument("--workload_mode", default="RANDOM", help="YCSB Workload mode (e.g., RANDOM, SEQ)")
parser.add_argument("--size", type=int, default=10, help="Size of the memory region in GiB")
parser.add_argument("--host_dram_size", type=str, default='6GiB', help="Size of the host DRAM in the CXL SSD")
parser.add_argument("--lat", type=int, default=256, help="CXL large access threshold in bytes")
args = parser.parse_args()

workload = args.workload
workload_mode = args.workload_mode
base_addr = 0
host_dram_size = args.host_dram_size
size = args.size * 1024**3
lat = args.lat

cfg_filename = f"ycsb_{workload.lower()}_smart.cfg"
create_ycsb_cfg(cfg_filename, workload, workload_mode, base_addr, size)

cpu = TrafficGen(config_file=cfg_filename)

system = System(
    cpu=cpu,
    membus=IOXBar(width=16),
    mem_ranges=[AddrRange(base_addr, base_addr+size)],
    clk_domain=SrcClockDomain(clock='1GHz', voltage_domain=VoltageDomain()),
    physmem=CxlSSD(
        range = AddrRange(base_addr, base_addr+size),
        # latency = "0",
        bandwidth = "32GiB/s",
        cxl_bandwidth = "32GiB/s",
        host_dram_size = host_dram_size,
        ssd_latency = "10us",
        cxl_large_access_threshold = lat
    )
)

system.cpu.port = system.membus.cpu_side_ports
system.physmem.port = system.membus.mem_side_ports

root = Root(full_system=False, system=system)
root.system.mem_mode = "timing"

print(f"--- Simulation Started ---")
print(f"Using config: {cfg_filename}")
print("System Memory Ranges in Python:", hex(system.mem_ranges[0].start), hex(system.mem_ranges[0].end))

m5.instantiate()
exit_event = m5.simulate(1_000_000_000_000)

if exit_event.getCause() != "simulate() limit reached":
    exit(1)