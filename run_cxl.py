import m5
from m5.objects import *
import sys
# [學長修正] 引入這個，才能查編譯時的設定 (像是 TARGET_ISA)
from m5.defines import buildEnv 

# 建立系統
system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '1GHz'
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('512MiB')] 

# System XBar
system.membus = SystemXBar()

# CPU
system.cpu = TimingSimpleCPU()
system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports

# ==========================================
# [學長修正] 改用 buildEnv 來檢查是不是 X86
# 這樣就不用管 ObjectList 有沒有被 import 了
# ==========================================
system.cpu.createInterruptController()
system.cpu.interrupts[0].pio = system.membus.mem_side_ports
system.cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
system.cpu.interrupts[0].int_responder = system.membus.mem_side_ports

# CxlSSD
system.mem_ctrl = CxlSSD()
system.mem_ctrl.range = system.mem_ranges[0]

# 參數設定
system.mem_ctrl.cxl_latency = '100ns'
system.mem_ctrl.ssd_latency = '80us'
system.mem_ctrl.host_dram_size = '256MiB'
system.mem_ctrl.cxl_dram_size = '256MiB' 
system.mem_ctrl.threshold_isolated = 8
system.mem_ctrl.threshold_distributed = 4

system.membus.mem_side_ports = system.mem_ctrl.port

# Process
process = Process()
process.cmd = ['./cxl_microbench']
system.cpu.workload = process
system.cpu.createThreads()

# Workload 初始化 (上一題教你的)
system.workload = SEWorkload.init_compatible(process.cmd[0])

root = Root(full_system = False, system = system)
m5.instantiate()

print("Beginning simulation!")
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")