#ifndef __MEM_CXL_SSD_HH__
#define __MEM_CXL_SSD_HH__

#include "mem/simple_mem.hh"
#include "params/CxlMemory.hh"
#include "mem/general_queue.hh"
#include "base/statistics.hh"

namespace gem5 {
namespace memory {

constexpr size_t PAGE_SIZE = 4096;  // 4KiB

struct CacheEntry {
    bool isDirty;
};

// Inherit SimpleMemory for TrafficGen
class CxlMemory : public SimpleMemory {
  private:
    // Cxl Parameter
    const Tick cxlLatency;
    const Tick hostLatency;

    // SSD Parameter
    const Tick ssdReadLatency;
    const Tick ssdWriteLatency;
    const Tick transferPenalty;

    // Sizes
    const size_t hostDramSize;
    const size_t cxlDramSize;

    // LRU Queues
    GeneralQueue<Addr, CacheEntry> hostQueue;
    GeneralQueue<Addr, CacheEntry> cxlInactiveQueue;
    GeneralQueue<Addr, CacheEntry> cxlActiveQueue;
    
    struct CxlMemoryStats : public statistics::Group {
        CxlMemoryStats(CxlMemory &cxl_mem);
        statistics::Scalar statHostHits;
        statistics::Scalar statCxlActiveHits;
        statistics::Scalar statCxlInactiveHits;
        statistics::Scalar statSsdMisses;
        statistics::Scalar statCleanEvicts;
        statistics::Scalar statWritebacks;
    } stats;

  public:
    using Params = CxlMemoryParams;
    CxlMemory(const Params &p);

  protected:
    bool recvTimingReq(PacketPtr pkt) override;
};

} // namespace memory
} // namespace gem5

#endif // __MEM_CXL_SSD_HH__