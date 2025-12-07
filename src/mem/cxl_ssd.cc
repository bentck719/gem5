#include "mem/cxl_ssd.hh"
#include "debug/CxlSSD.hh" // 記得註冊 Debug Flag

namespace gem5 {
namespace memory {

CxlSSD::CxlSSD(const Params &p)
    : SimpleMemory(p),
      cxlLatency(p.cxl_latency),
      cxlBandwidth(p.cxl_bandwidth),
      cxlDramSize(p.cxl_dram_size),
      ssdLatency(p.ssd_latency),
      hostLatency(p.host_latency),
      hostDramSize(p.host_dram_size),
      thresholdIsolated(p.threshold_isolated),
      thresholdDistributed(p.threshold_distributed),
      classifyQueue(cxlDramSize / CXL_SSD_PAGE_SIZE / 2),
      storeQueue(cxlDramSize / CXL_MEM_CHUNK_SIZE / 4),
      dirtyQueue(cxlDramSize / CXL_MEM_CHUNK_SIZE / 4),
      hostCache(hostDramSize / CXL_SSD_PAGE_SIZE)
{
    DPRINTF(CxlSSD, "CxlSSD Initialized with FIFO Queues\n");
}

// 從 Device Cache 移除，加入 Host Cache
void CxlSSD::moveToHost(Addr pageAddr) {
    // 1. Remove from HAIPC (可能在任何一個 Queue)
    classifyQueue.Remove(pageAddr);
    
    for (int i=0; i<16; i++) {
        Addr chunkAddr = pageAddr + (i * CXL_MEM_CHUNK_SIZE);
        storeQueue.Remove(chunkAddr);
        dirtyQueue.Remove(chunkAddr);
    }

    // 2. Add to HSPC (Host LRU)
    HostCacheEntry entry;
    hostCache.Insert(pageAddr, entry);
    
    DPRINTF(CxlSSD, "Migrated Page %#x to Host Cache\n", pageAddr);
}

Tick CxlSSD::anomalyHandler(Addr pageAddr, Addr chunkAddr, int chunkIdx, bool isWrite) {
    bool migrate = false;
    Tick migrateLatency = 0;

    ClassifyNode* cNode = classifyQueue.Get(pageAddr);
    ChunkNode* sNode = storeQueue.Get(chunkAddr);

    if (cNode) {
        cNode->chunk_bitmap.set(chunkIdx);

        if (cNode->access_counts[chunkIdx] < thresholdIsolated) cNode->access_counts[chunkIdx]++;

        // Check Distributed
        if (cNode->chunk_bitmap.count() > thresholdDistributed) migrate = true;
        // Check Isolated (Chunk in Classify)
        if (cNode->access_counts[chunkIdx] > thresholdIsolated) migrate = true;
        if (migrate) migrateLatency = transferPenalty4KB;
    }
    
    if (sNode) {
        if (sNode->access_count < thresholdIsolated) sNode->access_count++;
        if (sNode->access_count > thresholdIsolated) migrate = true;
        if (migrate) migrateLatency = transferPenalty4KB + ssdLatency;
    }

    if (migrate) {
        moveToHost(pageAddr);
    }

    addedLatency += cxlLatency

    return migrateLatency;
}

bool CxlSSD::recvTimingReq(PacketPtr pkt) {
    Addr addr = pkt->getAddr();
    Addr pageAddr = addr & ~(CXL_SSD_PAGE_SIZE - 1);
    Addr chunkAddr = addr & ~(CXL_MEM_CHUNK_SIZE - 1);
    int chunkIdx = (addr % CXL_SSD_PAGE_SIZE) / CXL_MEM_CHUNK_SIZE;
    bool isWrite = pkt->isWrite();
    
    Tick addedLatency = -latency;
    bool hit = false;

    // 1. Check Host Cache (HSPC)
    if (hostCache.Contains(pageAddr)) {
        hostCache.Remove(pageAddr);
        hostCache.Insert(pageAddr, HostCacheEntry());
        DPRINTF(CxlSSD, "Hit in Host Cache: %#x\n", addr);
        return SimpleMemory::recvTimingReq(pkt);
    }

    // 2. Check Device Cache (HAIPC)

    ChunkNode* dNode = dirtyQueue.Get(chunkAddr);
    if (dNode) {
        pkt->headerDelay += cxlLatency
        DPRINTF(CxlSSD, "Hit in Dirty Area: %#x\n", addr);
        return SimpleMemory::recvTimingReq(pkt);
    }

    ClassifyNode* cNode = classifyQueue.Get(pageAddr);
    ChunkNode* sNode = storeQueue.Get(chunkAddr);

    hit = (cNode | sNode);

    // Hit in CXL -> Standard CXL Latency (handled by SimpleMemory)
    if (hit) {
        pkt->headerDelay += anomalyHandler(pageAddr, chunkAddr, chunkIdx);
        DPRINTF(CxlSSD, "Hit in Device Cache: %#x\n", addr);
        return SimpleMemory::recvTimingReq(pkt);
    }

    // 3. Cache Miss (Flash Access)
    DPRINTF(CxlSSD, "Miss (Flash Access): %#x\n", addr);
    addedLatency = flashLatency;

    if (pkt->getSize() > 256) {
        // Large Access: SSD -> Host Cache
        HostCacheEntry victim;
        hostCache.Insert(pageAddr, victim); 
    } else {
        // Small Access -> Classify Area
        ClassifyNode newNode;
        newNode.chunk_bitmap.set(chunkIdx);
        newNode.access_counts[chunkIdx] = 1;
        
        auto victim = classifyQueue.Insert(pageAddr, newNode);
        
        if (victim.has_value()) {
            // 取出 Key (first) 和 Value (second)
            Addr victimAddr = victim->first;
            ClassifyNode& victimNode = victim->second;

            DPRINTF(CxlSSD, "Classify Eviction: Page %#x. Moving valid chunks to Store.\n", victimAddr);

            for (int i = 0; i < 16; i++) {
                if (victimNode.chunk_bitmap.test(i)) {
                    Addr chunkAddr = victimAddr + (i * CXL_MEM_CHUNK_SIZE);
                    
                    ChunkNode sNode;
                    sNode.access_count = victimNode.access_counts[i];
                    storeQueue.Insert(chunkAddr, sNode);
                }
            }
        }
    }
    

    // Apply Flash Penalty if needed
    pkt->headerDelay += addedLatency;

    // Let SimpleMemory handle the rest
    return SimpleMemory::recvTimingReq(pkt);
}

} // namespace memory
} // namespace gem5