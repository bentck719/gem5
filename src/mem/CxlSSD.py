from m5.params import *
from m5.objects.SimpleMemory import SimpleMemory

class CxlSSD(SimpleMemory):
    type = 'CxlSSD'
    cxx_header = "mem/cxl_ssd.hh"
    cxx_class = "gem5::memory::CxlSSD"

    # CXL & Host Parameters
    cxl_link_latency = Param.Latency('100ns', "CXL Link + Device DRAM Latency")
    
    # SSD Parameters
    nand_flash_read_latency = Param.Latency('100us', "Flash Read Latency (tR)")
    nand_flash_write_latency = Param.Latency('500us', "Flash Write Latency (tPROG)") 
    nand_flash_bandwidth = Param.MemoryBandwidth('2GB/s', "Nand Flash Bandwidth")
    modify_latency = Param.Latency('10us', "Internal Flash Modify Latency for RMW")

    # Queue/Cache Sizes
    byte_write_buffer_size = Param.MemorySize('4MiB', "In-device Memory Size")
    
    # 快取關聯度 (預設 8-way Set-Associative)
    associativity = Param.Unsigned(8, "Cache associativity (ways)")