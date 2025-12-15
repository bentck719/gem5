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

    FIFOQueue<Addr, ClassifyNode> classifyQueue; // Key: Page Aligned Addr
    FIFOQueue<Addr, ChunkNode> storeQueue;       // Key: Chunk Aligned Addr
    FIFOQueue<Addr, ChunkNode> dirtyQueue;       // Key: Chunk Aligned Addr
    
    FIFOQueue<Addr, HostCacheEntry> hostCache;   // Implement LRU logic in FIFOQueue
    

    // --- Helper ---
    void moveToHost(Addr pageAddr); // Migration logic
    void moveToDirty(Addr chunkAddr);
    void moveToStore(std::optional<std::pair<Addr, ClassifyNode>>& victim);
    std::optional<std::pair<Addr, ClassifyNode>> insertToClassify(Addr pageAddr, Addr chunkAddr, int chunkIdx, bool isWrite);
    void handleLargeAccess(Addr pageAddr);
    Tick handleCNode(Addr pageAddr, Addr chunkAddr, int chunkIdx, bool isWrite);
    Tick handleSNode(Addr chunkAddr, int chunkIdx, bool isWrite);

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