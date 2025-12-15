import m5
from m5.objects import *

# --- 1. 建立 System 與 Clock ---
system = System()
system.clk_domain = SrcClockDomain(clock='1GHz', voltage_domain=VoltageDomain())
system.mem_mode = 'timing'

# --- 2. 定義位址空間 ---
# slar0: Controller 自己的 Register 空間 (假設)
slar0 = AddrRange(start="0x400000000", size="1024MiB")
# slar: SSD 的記憶體空間 (TrafficGen 主要打這裡)
slar = AddrRange(start="0x440000000", size="10GiB")

# --- 3. 建立標準 System Bus (XBar) ---
# ★ 修正 1: 恢復 SystemXBar
system.membus = SystemXBar()

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

# ★ 修正 2: 設定 Link 範圍
# 這條 Link 在 Controller 和 Switch 之間，它必須讓 SSD 的流量通過！
# 所以 ranges 必須包含 slar
system.cxl_controller.seriallink = SerialLink(
    ranges=[slar0, slar], # 讓兩個範圍都通過
    req_size=16,          # 稍微加大一點比較保險
    resp_size=16,
    num_lanes=16,
    link_speed=32,        # PCIe Gen5 32GT/s
    delay="100ns",
)

# --- 6. 建立 CXL Device 端元件 ---
system.cxl_device = CXLDevice(
    width=16,
    frontend_latency=2,
    forward_latency=2,
    response_latency=4,
)

# Device 端的 Link
system.cxl_device.seriallink = SerialLink(
    ranges=slar, # 這裡只要 SSD 的範圍
    req_size=16,
    resp_size=16,
    num_lanes=16,
    link_speed=32,
    delay="100ns",
)

# CXL Switch
system.pciexbar = CXLXBar(
    width=16,
    frontend_latency=2,
    forward_latency=1,
    response_latency=2,
)

# Monitor
system.cxl_controller.monitor = CommMonitor()

# --- 7. 連接拓樸 (Topology) ---

# A. System Bus -> CXL Controller
# 這行告訴 membus: 0x40... 和 0x44... 的請求都丟給 Controller
system.membus.mem_side_ports = system.cxl_controller.cpu_side_ports

# B. CXL Controller -> Monitor -> SerialLink -> Switch
sl_host = system.cxl_controller.seriallink

system.cxl_controller.mem_side_ports = system.cxl_controller.monitor.cpu_side_port
system.cxl_controller.monitor.mem_side_port = sl_host.cpu_side_port
sl_host.mem_side_port = system.pciexbar.cpu_side_ports

# C. Switch -> SerialLink -> CXL Device
sl_device = system.cxl_device.seriallink

system.pciexbar.mem_side_ports = sl_device.cpu_side_port
sl_device.mem_side_port = system.cxl_device.cpu_side_ports

# --- 8. 設定 CxlSSD ---
system.cxl_ssd = SimpleMemory()
system.cxl_ssd.range = slar # 0x440000000 ~ +10GiB

# D. CXL Device -> CxlSSD
system.cxl_device.mem_side_ports = system.cxl_ssd.port

# --- 9. 執行 ---
root = Root(full_system=False, system=system)

print(f"--- Simulation Started ---")
print(f"Target Range: {slar}")

m5.instantiate()
exit_event = m5.simulate(1000000000000)

print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")

if exit_event.getCause() != "simulate() limit reached":
    exit(1)