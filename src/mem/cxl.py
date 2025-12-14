from m5.objects.XBar import *
from m5.params import *
from m5.objects.SimpleMemory import SimpleMemory


class CXLController(BaseXBar):
    type = "CXLController"
    cxx_header = "mem/cxl_controller.hh"
    cxx_class = "gem5::CXLController"

    # Host DRAM Parameter
    # Refer to SimpleMemory.py for Host DRAM Latency and Bandwidth
    hostLatency = Param.Latency('60ns', "Host DRAM  Latency")
    hostDramSize  = Param.MemorySize('6GiB', "In-device Memory Size")
    cxlChunkSize = Param.MemorySize('256B', "CXL Chunk Size in B")
    cxlLargeAccessThreshold = Param.Int(256, "Large Access Threshold")


class CXLDevice(BaseXBar):
    type = "CXLDevice"
    cxx_header = "mem/cxl_device.hh"
    cxx_class = "gem5::CXLDevice"


class CXLXBar(BaseXBar):
    type = "CXLXBar"
    cxx_header = "mem/cxlxbar.hh"
    cxx_class = "gem5::CXLXBar"


class CxlSSD(SimpleMemory):
    type = 'CxlSSD'
    cxx_header = "mem/cxl_ssd.hh"
    cxx_class = "gem5::memory::CxlSSD"

    # Cxl Parameter
    cxlBandwidth = Param.MemoryBandwidth('31.25GiB/s', "CXL Bandwidth")
    cxlDramSize  = Param.MemorySize('2GiB', "In-device Memory Size")
    cxlChunkSize = Param.MemorySize('256B', "CXL Chunk Size in B")

    # SSD Parameter
    ssdLatency = Param.Latency('10us', "Flash Read Latency")
    transferPenalty4KB = Param.Latency('131ns', "Transfer Penalty for 4KB")

    # Anomaly Detector Parameter
    thresholdIsolated = Param.Int(8, "Threshold for Isolated Hotspot")
    thresholdDistributed = Param.Int(4, "Threshold for Distributed Hotspots")

    cxlLargeAccessThreshold = Param.Int(256, "Large Access Threshold")