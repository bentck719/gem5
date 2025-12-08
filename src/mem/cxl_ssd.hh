#ifndef __MEM_CXL_SSD_HH__
#define __MEM_CXL_SSD_HH__

#include "mem/simple_mem.hh"
#include "params/CxlSSD.hh"
#include "mem/fifo_queue.hh"
#include <bitset>

namespace gem5 {
namespace memory {

constexpr uint32_t CXL_SSD_PAGE_SIZE = 4096;  // 4KiB
constexpr uint32_t CXL_MEM_CHUNK_SIZE = 256;  // 256B
constexpr uint32_t CXL_MEM_CHUNKS_PER_PAGE = CXL_SSD_PAGE_SIZE / CXL_MEM_CHUNK_SIZE;
constexpr uint32_t CXL_LARGE_ACCESS_THRESHOLD = 256;

struct ClassifyNode {
    std::bitset<CXL_MEM_CHUNKS_PER_PAGE> chunk_bitmap;
    std::bitset<CXL_MEM_CHUNKS_PER_PAGE> dirty_bitmap;
    std::vector<uint16_t> access_counts;
    ClassifyNode() : access_counts(CXL_MEM_CHUNKS_PER_PAGE, 0) {}
};

struct ChunkNode {
    uint16_t access_count;
    ChunkNode() : access_count(0) {}
};

struct HostCacheEntry {};

class CxlSSD : public SimpleMemory // 繼承 SimpleMemory 以支援 TrafficGen
{
  private:
    // Cxl Parameter
    const Tick cxlLatency;
    const double cxlBandwidth;
    const int cxlDramSize;

    // SSD Parameter
    const Tick ssdLatency;

    // Host DRAM Parameter
    const Tick hostLatency;
    const int hostDramSize;

    // Anomaly Detector Parameter
    const uint8_t thresholdIsolated;
    const uint8_t thresholdDistributed;

    const Tick transferPenalty4KB = 131000; // 131 ns
    const Tick transfetPenalty256B = 8000;  // 8ns
  
    FIFOQueue<Addr, ClassifyNode> classifyQueue; // Key: Page Aligned Addr
    FIFOQueue<Addr, ChunkNode> storeQueue;       // Key: Chunk Aligned Addr
    FIFOQueue<Addr, ChunkNode> dirtyQueue;       // Key: Chunk Aligned Addr

    FIFOQueue<Addr, HostCacheEntry> hostCache;   // Implement LRU logic in FIFOQueue

    // --- Helper ---
    void moveToHost(Addr pageAddr); // Migration logic
    Tick anomalyHandler(Addr pageAddr, Addr chunkAddr, int chunkIdx, ClassifyNode* cNode, ChunkNode* sNode);

  public:
    using Params = CxlSSDParams;
    CxlSSD(const Params &p);

  protected:
    bool recvTimingReq(PacketPtr pkt) override;
};

} // namespace memory
} // namespace gem5

#endif // __MEM_CXL_SSD_HH__