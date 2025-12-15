import m5
from m5.objects import *

# --- 1. 建立 System 與 Clock ---
system = System()
system.clk_domain = SrcClockDomain(clock='1GHz', voltage_domain=VoltageDomain())

# --- 2. 定義位址空間 ---
start_addr = 0x0
addr_size = '16GiB'
# slar0: Controller 自己的 Register 空間 (假設)
slar0 = AddrRange(start=start_addr, size="1024MiB")
# slar: SSD 的記憶體空間 (TrafficGen 主要打這裡)
slar = AddrRange(start=(start_addr + 0x40000000), size="10GiB")

# --- 3. 建立標準 System Bus (XBar) ---
system.membus = SystemXBar()
system.mem_mode = 'timing'
system.mem_ranges = [slar0, slar]

# --- 4. 設定 TrafficGen CPU ---
cfg_filename = "ycsb_workload_A.cfg"
system.cpu = TrafficGen(config_file=cfg_filename)
system.cpu.port = system.membus.cpu_side_ports

# --- 5. 建立 CXL Controller ---
system.cxl_controller = CXLController(
    width=16,
    frontend_latency=2,
    forward_latency=3,
    response_latency=3,
)

system.cxl_controller.seriallink = SerialLink(
    ranges=[slar],
    req_size=16,         
    resp_size=16,
    num_lanes=16,
    link_speed=32, 
    delay="100ns",
)

# --- 7. 連接拓樸 (Topology) ---

sl_host = system.cxl_controller.seriallink

# A. System Bus -> CXL Controller
system.membus.mem_side_ports = system.cxl_controller.cpu_side_ports
system.cxl_controller.mem_side_ports = sl_host.cpu_side_port

# --- 8. 設定 CxlSSD ---
system.cxl_ssd = SimpleMemory()
system.cxl_ssd.range = slar
sl_host.mem_side_port = system.cxl_ssd.port

# --- 9. 執行 ---
root = Root(full_system=False, system=system)

print(f"--- Simulation Started ---")
print(f"Using config: {cfg_filename}")
print("System Memory Ranges in Python:", hex(system.mem_ranges[0].start), hex(system.mem_ranges[0].end))

m5.instantiate()
exit_event = m5.simulate(1_000_000_000_000)

print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")

if exit_event.getCause() != "simulate() limit reached":
    exit(1)