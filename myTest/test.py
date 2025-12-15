# Copyright (c) 2021-24 The Regents of the University of California
# All rights reserved.
#
# ... (License header omitted for brevity) ...

"""
TrafficGen with Config File + CXL System
"""

from m5.objects import *

# --- 1. 建立基礎 System ---
system = System()

system.clk_domain = SrcClockDomain(
    clock='1GHz',
    voltage_domain=VoltageDomain()
)
system.membus = SystemXBar()
system.system_port = system.membus.cpu_side_ports

# --- 2. TrafficGen 配置 (使用外部 Config 檔案) ---
# 定義設定檔路徑 (請確保此檔案存在於執行目錄下，或提供絕對路徑)
cfg_filename = "ycsb_workload_A.cfg"

# 使用 TrafficGen 作為系統的 CPU
system.cpu = TrafficGen(config_file=cfg_filename)

# 將 TrafficGen 連接到系統匯流排
system.cpu.port = system.membus.cpu_side_ports

# --- 3. CXL 和記憶體組件配置 ---
# 定義地址範圍
slar0 = AddrRange(start="0x400000000", size="1024MiB")  # CXL controller
slar = AddrRange(start="0x440000000", size="8192MiB")   # CXL device 1 (Target for traffic)
slar2 = AddrRange(start="0x640000000", size="8192MiB")  # CXL device 2 (Target for traffic)

# Create CXL components
system.cxl_controller = CXLController(
    width=16,
    frontend_latency=2,
    forward_latency=3,
    response_latency=3,
)
system.cxl_device = CXLDevice(
    width=16,
    frontend_latency=2,
    forward_latency=2,
    response_latency=4,
)
system.cxl_controller.seriallink = SerialLink(
    ranges=[slar0, slar, slar2],
    req_size=10,
    resp_size=10,
    num_lanes=16,
    link_speed=31,
    delay="100ns",
)
system.cxl_device.seriallink = SerialLink(
    ranges=slar,
    req_size=10,
    resp_size=10,
    num_lanes=16,
    link_speed=31,
    delay="100ns",
)
system.pciexbar = CXLXBar(
    width=16,
    frontend_latency=2,
    forward_latency=1,
    response_latency=2,
)
system.pciexbar2 = CXLXBar(
    width=16,
    frontend_latency=2,
    forward_latency=1,
    response_latency=2,
)
system.cxl_device2 = CXLDevice(
    width=16,
    frontend_latency=2,
    forward_latency=2,
    response_latency=4,
)
system.cxl_device2.seriallink = SerialLink(
    ranges=slar2,
    req_size=10,
    resp_size=10,
    num_lanes=16,
    link_speed=31,
    delay="100ns",
)
system.pciexbar2.seriallink = SerialLink(
    ranges=slar2,
    req_size=10,
    resp_size=10,
    num_lanes=16,
    link_speed=31,
    delay="100ns",
)

system.cxl_controller.monitor = CommMonitor()

# Connect the components
# xbar -> CXL Controller
system.membus.mem_side_ports = system.cxl_controller.cpu_side_ports

sl = system.cxl_controller.seriallink
system.cxl_controller.mem_side_ports = (
    system.cxl_controller.monitor.cpu_side_port
)
system.cxl_controller.monitor.mem_side_port = sl.cpu_side_port
sl.mem_side_port = system.pciexbar.cpu_side_ports

# cxl board 1 connection
sl2 = system.cxl_device.seriallink
# cxl board 2 connection
sl3 = system.pciexbar2.seriallink
sl4 = system.cxl_device2.seriallink

# 連接 pciexbar (Crossbar) 到下層鏈路
# 注意：CXLXBar.mem_side_ports 是 VectorPort，將兩個下游鏈路連上
system.pciexbar.mem_side_ports = [sl2.cpu_side_port, sl3.cpu_side_port]

sl2.mem_side_port = system.cxl_device.cpu_side_ports

sl3.mem_side_port = system.pciexbar2.cpu_side_ports
system.pciexbar2.mem_side_ports = sl4.cpu_side_port
sl4.mem_side_port = system.cxl_device2.cpu_side_ports

# Memory controller setup
system.mem_ctrl = MemCtrl()
mc = system.mem_ctrl
mc.dram = DDR3_1600_8x8()
mc.dram.range = slar  # Match slar
mc.port = system.cxl_device.mem_side_ports

# system.cxl_ssd1 = CxlSSD(
#     range = slar
# )
# system.cxl_ssd1.port = system.cxl_device.mem_side_ports

system.mem_ctrl2 = MemCtrl()
mc2 = system.mem_ctrl2
mc2.dram = DDR3_1600_8x8()
mc2.dram.range = slar2  # Match slar2
system.cxl_device2.mem_side_ports = mc2.port

# --- 9. 執行 ---

root = Root(full_system=False, system=system)
root.system.mem_mode = "timing"

print(f"--- Simulation Started ---")
print(f"Target Range: {hex(slar.start)} - {hex(slar.end)} and {hex(slar2.start)} - {hex(slar2.end)}")
print(f"Target Range: {(slar.start)} - {(slar.end)} and {(slar2.start)} - {(slar2.end)}")

m5.instantiate()
exit_event = m5.simulate(10**10)

print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")

if exit_event.getCause() != "simulate() limit reached":
    exit(1)