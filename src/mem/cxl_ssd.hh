#ifndef __MEM_CXL_SSD_HH__
#define __MEM_CXL_SSD_HH__

#include <vector>
#include <cstdint>
#include "mem/simple_mem.hh"
#include "params/CxlSSD.hh" 
#include "base/statistics.hh"

namespace gem5 {
namespace memory {

constexpr size_t PAGE_SIZE = 4096;      // 4KiB
constexpr size_t CACHELINE_SIZE = 64;   // 64 Bytes

// 極致輕量的 Metadata Entry (模擬 SSD 內部的 SRAM Tag Store)
struct SsdMetadataEntry {
    uint64_t tag = 0;           // 標籤 (Address Tag)
    uint64_t bitmap = 0;        // 64-bit Bitmap (追蹤哪個 64B 被修改)
    bool valid = false;         // 該 Entry 是否有效
    Tick lastTick = 0;          // 紀錄最後存取時間，用於 LRU 替換策略
};

// 統一類別名稱為 CxlSSD
class CxlSSD : public SimpleMemory {
  private:
    // Cxl Parameter
    const Tick cxlLinkLatency;

    // SSD Parameter
    const Tick nandFlashReadLatency;
    const Tick nandFlashWriteLatency;
    const double nandFlashBandwidth;
    const Tick nandFlashTransferLatency; // 2 GiB/s
    const Tick modifyLatency;
    Tick readModifyWriteLatency;

    // Cache Architecture Parameters
    const size_t byteWriteBufferSize;
    const uint32_t numWays;
    const uint32_t numSets;

    uint32_t setShift;
    uint64_t setMask;
    uint32_t tagShift;

    // metadata_cache[NUM_SETS][WAYS]
    std::vector<std::vector<SsdMetadataEntry>> metadataCache;
    
    struct CxlSSDStats : public statistics::Group {
        CxlSSDStats(CxlSSD &cxl_ssd);
        statistics::Scalar statBufferHits;       
        statistics::Scalar statBufferMisses;     
        statistics::Scalar statEvictions;      // 踢出舊 Page 的次數
        // 為 Bitmap 雙路徑專屬的統計數據
        statistics::Scalar statRmwOperations;
        statistics::Scalar statByteWrite;
        statistics::Scalar statBlockWrite;
        statistics::Scalar statBlockRead;

        statistics::Scalar statByteWriteLatency;
        statistics::Scalar statBlockWriteLatency;
        statistics::Scalar statBlockReadLatency;

        statistics::Scalar statSyncCmd;
      } stats;

  public:
    using Params = CxlSSDParams;
    CxlSSD(const Params &p);

  protected:
    bool recvTimingReq(PacketPtr pkt) override;
    
    // 內部 Helper Functions (方便實作 LRU)
    uint32_t extractSetIdx(Addr addr) const;
    uint64_t extractTag(Addr addr) const;
    uint32_t extractCachelineIdx(Addr addr) const;
};

} // namespace memory
} // namespace gem5

#endif // __MEM_CXL_SSD_HH__