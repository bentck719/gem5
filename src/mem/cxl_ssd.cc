#include "mem/cxl_ssd.hh"
#include "debug/CxlSSD.hh"
#include "base/trace.hh"
#include "base/intmath.hh"

namespace gem5 {
namespace memory {

#define FTL_BASE_ADDR  0x200000000ULL
#define DRAM_BASE_ADDR 0x400000000ULL

CxlSSD::CxlSSD(const Params &p)
    : SimpleMemory(p),
      cxlLinkLatency(p.cxl_link_latency),
      nandFlashReadLatency(p.nand_flash_read_latency),
      nandFlashWriteLatency(p.nand_flash_write_latency),
      nandFlashBandwidth(p.nand_flash_bandwidth),
      nandFlashTransferLatency(nandFlashBandwidth * PAGE_SIZE),
      modifyLatency(p.modify_latency),
      byteWriteBufferSize(p.byte_write_buffer_size),
      numWays(p.associativity),
      numSets((p.byte_write_buffer_size / PAGE_SIZE) / p.associativity),
      stats(*this)
{
    fatal_if(!isPowerOf2(numSets), "CxlSSD: numSets (%d) must be a power of 2!", numSets);

    setShift = floorLog2(PAGE_SIZE);
    setMask  = numSets - 1;
    tagShift = setShift + floorLog2(numSets);

    metadataCache.resize(numSets, std::vector<SsdMetadataEntry>(numWays));

    readModifyWriteLatency = nandFlashReadLatency + 
                            nandFlashTransferLatency + 
                            modifyLatency +
                            nandFlashTransferLatency +
                            nandFlashWriteLatency;
    
    std::cout << "[CxlSSD] Initialized Set-Associative Bitmap Cache." << std::endl;
    std::cout << "[CxlSSD] Capacity: " << byteWriteBufferSize / (1024*1024) << " MB, "
              << "Sets: " << numSets << ", Ways: " << numWays << std::endl;
}

CxlSSD::CxlSSDStats::CxlSSDStats(CxlSSD &cxl_ssd)
    : statistics::Group(&cxl_ssd),
      ADD_STAT(statBufferHits, statistics::units::Count::get(), "Number of hits in Byte Write Buffer"),
      ADD_STAT(statBufferMisses, statistics::units::Count::get(), "Number of Misses in Byte Write Buffer"),
      ADD_STAT(statEvictions, statistics::units::Count::get(), "Number of page evictions from Byte Write Buffer"),
      ADD_STAT(statRmwOperations, statistics::units::Count::get(), "Number of Read-Modify-Write operations"),
      ADD_STAT(statByteWrite, statistics::units::Count::get(), "Number of CXL.mem byte-level writes"),
      ADD_STAT(statBlockWrite, statistics::units::Count::get(), "Number of Block I/O Write to Flash"),
      ADD_STAT(statBlockRead, statistics::units::Count::get(), "Number of Block I/O Read to Flash"),

      ADD_STAT(statByteWriteLatency, statistics::units::Count::get(), "Total Latency for Byte Writes"),
      ADD_STAT(statBlockWriteLatency, statistics::units::Count::get(), "Total Latency for Block Writes"),
      ADD_STAT(statBlockReadLatency, statistics::units::Count::get(), "Total Latency for Block Read"),

      ADD_STAT(statSyncCmd, statistics::units::Count::get(), "Flag for Sync Smd")
{
}

uint32_t CxlSSD::extractSetIdx(Addr addr) const {
    return (addr >> setShift) & setMask;
}

uint64_t CxlSSD::extractTag(Addr addr) const {
    return addr >> tagShift;
}

uint32_t CxlSSD::extractCachelineIdx(Addr addr) const {
    return (addr >> 6) & 0x3F;
}

bool CxlSSD::recvTimingReq(PacketPtr pkt) {
    panic_if(pkt->cacheResponding(), "Should not see packets where cache is responding");
    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "Should only see read and writes at memory controller, "
             "saw %s to %#llx\n", pkt->cmdString(), pkt->getAddr());

    if (retryReq) 
        return false;
    if (isBusy) {
        retryReq = true;
        return false;
    }

    Addr paddr = pkt->getAddr();

    bool is_sync_cmd = false;
    if (paddr == (DRAM_BASE_ADDR - 64) && pkt->isWrite() && pkt->hasData()) {
        
        // 取得封包資料的指標 (讀取為 8-bit unsigned integer)
        const uint8_t* pkt_data = pkt->getConstPtr<uint8_t>();
        
        // 檢查 Payload 的第一個 byte 是否為 Magic Number (0xFF)
        if (pkt_data[0] == 0xFF) {
            is_sync_cmd = true;
            stats.statSyncCmd++;
        }
    }

    bool is_byte_io  = (paddr >= DRAM_BASE_ADDR);
    bool is_block_io = (paddr >= FTL_BASE_ADDR && paddr < DRAM_BASE_ADDR);

    Addr relative_offset = 0;
    if (is_byte_io) relative_offset = paddr - DRAM_BASE_ADDR;
    else if (is_block_io) relative_offset = paddr - FTL_BASE_ADDR;

    Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
    pkt->headerDelay = pkt->payloadDelay = 0;

    Tick duration = 0;
    if (pkt->isWrite()) {
        if (is_block_io) {
            duration += PAGE_SIZE * bandwidth;
            stats.statBlockWriteLatency += duration;
        }
        else if (is_byte_io) {
            duration += CACHELINE_SIZE * bandwidth;
            stats.statByteWriteLatency += duration;
        }
    }
    else {
        duration  += pkt->getSize() * bandwidth; // protocol head count
        stats.statBlockReadLatency += duration;
    }
    
    if (duration != 0) {
        schedule(releaseEvent, curTick() + duration);
        isBusy = true;
    }

    bool needsResponse = pkt->needsResponse();
    recvAtomic(pkt);

    if (needsResponse) {
        assert(pkt->isResponse());
        Tick dynamicLatency = 0;

        if (is_sync_cmd) {
            int flushCount = 0;
            for (int s = 0; s < numSets; ++s) {
                for (int w = 0; w < numWays; ++w) {
                    // 如果這個 Cacheline 有效且含有髒資料
                    if (metadataCache[s][w].valid && metadataCache[s][w].bitmap != 0) {
                        dynamicLatency += readModifyWriteLatency; // 結算 RMW 代價
                        flushCount++;
                        metadataCache[s][w].valid = false;
                        metadataCache[s][w].bitmap = 0;
                    }
                }
            }
            stats.statRmwOperations += flushCount;
            stats.statEvictions += flushCount;
            stats.statBlockWriteLatency += dynamicLatency; // 算入總硬體延遲
        }
        else {
            uint32_t setIdx = extractSetIdx(relative_offset);
            uint64_t tag    = extractTag(relative_offset);
            uint32_t clIdx  = extractCachelineIdx(relative_offset);
    
            // Path A: CXL.mem 微粒度寫入 (Bitmap 更新)
            if (is_byte_io) {
                bool hit = false;
                int hitWay = -1;
                int emptyWay = -1;
                int lruWay = 0;
                Tick oldestTick = curTick();
    
                // 尋找 Set 內部的 Ways
                for (int w = 0; w < numWays; ++w) {
                    if (metadataCache[setIdx][w].valid) {
                        if (metadataCache[setIdx][w].tag == tag) {
                            hit = true;
                            hitWay = w;
                            break;
                        }
                        if (metadataCache[setIdx][w].lastTick < oldestTick) {
                            oldestTick = metadataCache[setIdx][w].lastTick;
                            lruWay = w;
                        }
                    } 
                    else if (emptyWay == -1) {
                        emptyWay = w; // 紀錄第一個空位
                    }
                }
    
                dynamicLatency += cxlLinkLatency + bandwidth*CACHELINE_SIZE;
    
                // Hit: O(1) 更新 Bitmap
                if (hit) {
                    stats.statBufferHits++;
                    metadataCache[setIdx][hitWay].bitmap |= (1ULL << clIdx);
                    metadataCache[setIdx][hitWay].lastTick = curTick();
                }
                // Miss: 觸發 LRU 替換與 SSD Fetch
                else {
                    stats.statBufferMisses++;
                    int targetWay = (emptyWay != -1) ? emptyWay : lruWay;
    
                    // Page Eviction
                    if (metadataCache[setIdx][targetWay].valid) {
                        stats.statEvictions++;
                        uint64_t victim_bitmap = metadataCache[setIdx][targetWay].bitmap;
                        
                        // 如果被踢出的 Page 含有髒資料，計算 RMW 延遲懲罰
                        if (victim_bitmap != 0) {
                            stats.statRmwOperations++;
                            dynamicLatency += readModifyWriteLatency;
                        }
                    }
                    
                    metadataCache[setIdx][targetWay].valid = true;
                    metadataCache[setIdx][targetWay].tag = tag;
                    metadataCache[setIdx][targetWay].bitmap = (1ULL << clIdx); // 設定新的 Dirty bit
                    metadataCache[setIdx][targetWay].lastTick = curTick();
                }
                stats.statByteWriteLatency += dynamicLatency;
                stats.statByteWrite++;
            }
            // Path B: Block I/O 寫入 (直接寫回 FTL)
            else if (is_block_io) {
                // block read
                if (pkt->isRead()) {
                    dynamicLatency += nandFlashReadLatency + 
                                    nandFlashTransferLatency +
                                    bandwidth * PAGE_SIZE; // block transfer back to host
                    stats.statBlockRead++;
                    stats.statBlockReadLatency += dynamicLatency;
                }
                // block write
                else {
                    dynamicLatency += readModifyWriteLatency;
                    stats.statBlockWrite++;
                    stats.statRmwOperations++;
                    stats.statBlockWriteLatency += dynamicLatency;
                }
    
                // Since the read has fetched or the read has flushed, 
                // if the metadataCache has data in the same page, 
                // it has to reset it to empty;
                for (int w = 0; w < numWays; ++w) {
                    if (metadataCache[setIdx][w].valid) {
                        if (metadataCache[setIdx][w].tag == tag) {
                            metadataCache[setIdx][w].valid = false;
                            break;
                        }
                    }
                }
            }
        }

        // 計算封包回應時間並推入 Queue
        Tick when_to_send = curTick() + receive_delay + dynamicLatency;

        auto i = packetQueue.end();
        --i;
        while (i != packetQueue.begin() && when_to_send < i->tick && !i->pkt->matchAddr(pkt)) {
            --i;
        }

        packetQueue.emplace(++i, pkt, when_to_send);

        if (!retryResp && !dequeueEvent.scheduled()) {
            schedule(dequeueEvent, packetQueue.back().tick);
        }

    } 
    else {
        pendingDelete.reset(pkt);
    }

    return true;
}

} // namespace memory
} // namespace gem5