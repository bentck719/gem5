#ifndef __MEM_CXL_SSD_HH__
#define __MEM_CXL_SSD_HH__

#include "mem/simple_mem.hh"
#include "params/CxlSSD.hh"
#include "mem/general_queue.hh"
#include <bitset>

namespace gem5 {
namespace memory {

constexpr uint32_t CXL_SSD_PAGE_SIZE = 4096;  // 4KiB
constexpr uint32_t CXL_MEM_CHUNK_SIZE = 256;  // 256B
constexpr uint32_t CXL_MEM_CHUNKS_PER_PAGE = CXL_SSD_PAGE_SIZE / CXL_MEM_CHUNK_SIZE;

struct AddrPacket {
    Addr pageAddr;
    Addr pageAddrEnd;
    Addr chunkAddr;
    Addr chunkAddrEnd;
    int chunkIdx;

    AddrPacket(PacketPtr pkt) {
        Addr addr = pkt->getAddr();
        size_t size = pkt->getSize();

        pageAddr = addr & ~(CXL_SSD_PAGE_SIZE - 1);
        pageAddrEnd = (addr + size - 1) & ~(CXL_SSD_PAGE_SIZE - 1);
        chunkAddr = addr & ~(CXL_MEM_CHUNK_SIZE - 1);
        chunkAddrEnd = (addr + size - 1) & ~(CXL_MEM_CHUNK_SIZE - 1);
        chunkIdx = (addr % CXL_SSD_PAGE_SIZE) / CXL_MEM_CHUNK_SIZE;
    }

    bool isLargeAccess() const {
        return pageAddr != pageAddrEnd || chunkAddr != chunkAddrEnd;
    }
};

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

// Inherit SimpleMemory for TrafficGen
class CxlSSD : public SimpleMemory {
  private:
    // Cxl Parameter
    const Tick cxlLatency;
    const double cxlBandwidth;
    const size_t cxlDramSize;

    // SSD Parameter
    const Tick ssdLatency;
    const Tick transferPenalty4KB; // 131 ns

    // Host DRAM Parameter
    const Tick hostLatency;
    const size_t hostDramSize;

    // Anomaly Detector Parameter
    const uint8_t thresholdIsolated;
    const uint8_t thresholdDistributed;

    const uint16_t cxlLargeAccessThreshold;

    GeneralQueue<Addr, ClassifyNode> classifyQueue; // Key: Page Aligned Addr
    GeneralQueue<Addr, ChunkNode> storeQueue;       // Key: Chunk Aligned Addr
    GeneralQueue<Addr, ChunkNode> dirtyQueue;       // Key: Chunk Aligned Addr
    
    GeneralQueue<Addr, HostCacheEntry> hostCache;   // Implement LRU logic in GeneralQueue
    

    // --- Helper ---
    void moveToHost(Addr chunkAddr); // Migration logic
    void moveToDirty(Addr chunkAddr);
    void moveToStore(std::optional<std::pair<Addr, ClassifyNode>>& victim);
    std::optional<std::pair<Addr, ClassifyNode>> insertToClassify(const AddrPacket& info, bool isWrite);
    void handleLargeAccess(Addr pageAddr);
    Tick handleCNode(const AddrPacket& info, bool isWrite);
    Tick handleSNode(const AddrPacket& info, bool isWrite);

  public:
    using Params = CxlSSDParams;
    CxlSSD(const Params &p);

  protected:
    bool recvTimingReq(PacketPtr pkt) override;

    struct CxlStats : public statistics::Group {
        CxlStats(statistics::Group *parent);

        // 1. 計數器 (Count)：發生了幾次？
        statistics::Scalar readHitsHost;
        statistics::Scalar readHitsClassify;
        statistics::Scalar readHitsStore;
        statistics::Scalar readHitsDirty;
        statistics::Scalar readMisses;        
        statistics::Scalar largeAccesses;     
        statistics::Scalar smallAccesses;
        statistics::Scalar migrations;        
        statistics::Scalar crossPageAccesses;

        // 2. 累加器 (Average)：總共花了多少時間？(用來算平均延遲)
        statistics::Scalar totalLatency;      
        statistics::Scalar migrationLatency;
        statistics::Scalar hitsHostLatency;
        statistics::Scalar hitsClassifyLatency;
        statistics::Scalar hitsStoreLatency;
        statistics::Scalar hitsDirtyLatency;
        statistics::Scalar largeAccessLatency;
        statistics::Scalar smallAccessLatency;
        
        // 3. 直方圖 (Histogram)：延遲的分佈圖 (論文神器！)
        // 可以看出有沒有長尾延遲 (Tail Latency)
        statistics::Histogram latencyDistribution;
    } stats;
};

} // namespace memory
} // namespace gem5

#endif // __MEM_CXL_SSD_HH__