from m5.params import *
from m5.objects.SimpleMemory import SimpleMemory

class CxlSSD(SimpleMemory):
    type = 'CxlSSD'
    cxx_header = "mem/cxl_ssd.hh"
    cxx_class = "gem5::memory::CxlSSD"

    # Cxl Parameter
    cxl_latency = Param.Latency('100ns', "CXL Link Latency")
    cxl_bandwidth = Param.MemoryBandwidth('31.25GiB/s', "CXL Bandwidth")
    cxl_dram_size  = Param.MemorySize('2GiB', "In-device Memory Size")

    # SSD Parameter
    ssd_latency = Param.Latency('10us', "Flash Read Latency")
    transfer_penalty4KB = Param.Latency('131ns', "Transfer Penalty for 4KB")

    # Host DRAM Parameter
    # Refer to SimpleMemory.py for Host DRAM Latency and Bandwidth
    host_latency = Param.Latency('60ns', "Host DRAM  Latency")
    host_dram_size  = Param.MemorySize('6GiB', "In-device Memory Size")

    # Anomaly Detector Parameter
    threshold_isolated = Param.Int(8, "Threshold for Isolated Hotspot")
    threshold_distributed = Param.Int(4, "Threshold for Distributed Hotspots")

    cxl_large_access_threshold = Param.Int(256, "Large Access Threshold")
    