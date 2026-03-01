from m5.params import *
from m5.objects.SimpleMemory import SimpleMemory

class CxlMemory(SimpleMemory):
    type = 'CxlMemory'
    cxx_header = "mem/cxl_memory.hh"
    cxx_class = "gem5::memory::CxlMemory"

    # CXL & Host Parameters
    cxl_latency = Param.Latency('100ns', "CXL Link Latency")
    host_latency = Param.Latency('80ns', "Host DRAM Latency")
    
    # SSD Parameters
    ssd_read_latency = Param.Latency('100us', "Flash Read Latency")
    ssd_write_latency = Param.Latency('500us', "Flash Write Latency") 
    transfer_penalty4KB = Param.Latency('131ns', "Transfer Penalty for 4KB")

    # Queue/Cache Sizes
    cxl_dram_size  = Param.MemorySize('2GiB', "In-device Memory Size")
    host_dram_size  = Param.MemorySize('6GiB', "Host DRAM Size")
