import m5
from m5.objects import *
from gen_ycsb_trace import create_ycsb_cfg

workload = "A"
base_addr = 0
size = 10 * 1024**3

cfg_filename = f"ycsb_{workload.lower()}_smart.cfg"
create_ycsb_cfg(cfg_filename, workload, base_addr, size)

cpu = TrafficGen(config_file=cfg_filename)

system = System(
    cpu=cpu,
    membus=IOXBar(width=16),
    mem_ranges=[AddrRange(base_addr, base_addr+size)],
    clk_domain=SrcClockDomain(clock='2GHz', voltage_domain=VoltageDomain()),
    physmem=CxlSSD(
        range = AddrRange(base_addr, base_addr+size),
        latency = "0",
        bandwidth = "32GiB/s"
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