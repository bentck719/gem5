from m5.objects import *
from m5.util.convert import toMemorySize
from gem5.components.boards.x86_board import X86Board
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.cachehierarchies.classic.private_l1_shared_l2_cache_hierarchy import (
    PrivateL1SharedL2CacheHierarchy,
)
from gem5.resources.resource import BinaryResource, DiskImageResource
from gem5.simulate.simulator import Simulator
from gem5.simulate.exit_event import ExitEvent
from gem5.utils.requires import requires
from gem5.isas import ISA

from gem5.components.memory.abstract_memory_system import AbstractMemorySystem
from gem5.utils.override import overrides

class CxlMemory(AbstractMemorySystem):    
    def __init__(self, size: str, latency: str = "80ns", **kwargs) -> None:
        super().__init__()
        self._size_str = size
        host_dram_size = kwargs.get("host_dram_size", "6GiB")
        large_access_threshold = kwargs.get("cxl_large_access_threshold", 256)

        # --- 這裡就是你的自定義 SimObject ---
        # 你可以在這裡隨意修改 latency, bandwidth，或是換成別的物件
        self.physmem = CxlSSD(
            range = AddrRange(start="0", size=size),
            latency = latency,
            bandwidth = "32GiB/s",
            cxl_bandwidth = "32GiB/s",
            host_dram_size = host_dram_size,
            ssd_latency = "100us",
            cxl_large_access_threshold = large_access_threshold
        )
        # ----------------------------------

    @overrides(AbstractMemorySystem)
    def get_size(self) -> int:
        return toMemorySize(self._size_str)

    @overrides(AbstractMemorySystem)
    def set_memory_range(self, ranges):
        """
        X86Board 算好記憶體區間後，會呼叫這個方法通知你。
        我們要把這個區間設定給底層的 SimObject。
        """
        # ranges 是一個 list，通常只有一個區間，我們拿第一個就好
        if ranges:
            self.physmem.range = ranges[0]
        else:
            raise Exception("Received empty memory range")

    @overrides(AbstractMemorySystem)
    def get_mem_interfaces(self):
        """
        X86Board 問：「你的連接孔在哪裡？」
        我們要回傳底層 SimObject 的 port。
        """
        return [self.physmem.port]

requires(
    isa_required=ISA.X86,
)

import argparse

# --- 參數設定 ---
parser = argparse.ArgumentParser(description="CXL SSD Full System Simulation with YCSB")
parser.add_argument("--script", default="", help="Path to the auto-run script (e.g., ycsb_test.sh)")
parser.add_argument("--host-dram-size", default='6GiB', help="Size of the host DRAM buffer in CXL SSD")
parser.add_argument("--lat", type=int, default=256, help="CXL large access threshold")
parser.add_argument("--checkpoint-dir", default=None, help="Directory to save/restore checkpoints")
parser.add_argument("--restore", action="store_true", help="Restore from checkpoint instead of booting")
args = parser.parse_args()

KERNEL = "vmlinux"
DISK_IMAGE = "/home/ben/Documents/io-exp/simulation/qemu-kvm/images/vm.qcow2"

if args.restore:
    print("--- Mode: TIMING (Restore) ---")
    cpu_type = CPUTypes.TIMING
else:
    print("--- Mode: KVM (Boot) ---")
    cpu_type = CPUTypes.KVM

processor = SimpleProcessor(cpu_type=cpu_type, isa=ISA.X86, num_cores=2)

cache_hierarchy = PrivateL1SharedL2CacheHierarchy(
    l1d_size="64KiB", l1i_size="64KiB", l2_size="8MiB"
)

memory = CxlMemory(
    size="32GiB",
    host_dram_size=args.host_dram_size,
    cxl_large_access_threshold=args.lat
)

board = X86Board(
    clk_freq="3GHz",
    processor=processor,
    cache_hierarchy=cache_hierarchy,
)

# xbar = board.get_cache_hierarchy().membus
# xbar.mem_side_ports = memory.port

board.set_kernel_disk_workload(
    kernel=BinaryResource(KERNEL),
    disk_image=DiskImageResource(DISK_IMAGE),
    readfile=args.script if args.script else None,
    kernel_args=["console=ttyS0", "root=/dev/hda1", "rw"]
)

def checkpoint_handler():
    print("--- Checkpoint Triggered ---")
    if not args.restore:
        print(f"Saving checkpoint to {args.checkpoint_dir}...")
        simulator.save_checkpoint(args.checkpoint_dir)
        return True # 存完檔退出
    return False # 恢復模式下遇到 checkpoint 則忽略

def exit_handler():
    print("--- Guest Exited (m5 exit) ---")
    return True

simulator = Simulator(
    board=board,
    checkpoint_path=args.checkpoint_dir if args.restore else None,
    on_exit_event={
        ExitEvent.CHECKPOINT: checkpoint_handler,
        ExitEvent.EXIT: exit_handler,
    }
)

if args.restore:
    print("Restoring...")
else:
    print("Booting...")

simulator.run()