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

void CxlSSD::moveToHost(Addr pageAddr) {
    // 1. Remove from HAIPC
    classifyQueue.Remove(pageAddr);
    
    for (int i=0; i<CXL_MEM_CHUNKS_PER_PAGE; i++) {
        Addr chunkAddr = pageAddr + (i * CXL_MEM_CHUNK_SIZE);
        storeQueue.Remove(chunkAddr);
        dirtyQueue.Remove(chunkAddr);
    }

    // 2. Add to HSPC (Host LRU)
    HostCacheEntry entry;
    hostCache.Insert(pageAddr, entry);
    
    DPRINTF(CxlSSD, "Migrated Page %#x to Host Cache\n", pageAddr);
}

void CxlSSD::moveToDirty(Addr chunkAddr) {
    ChunkNode* dNode;
    dirtyQueue.Insert(chunkAddr, dNode);
    return;
}

// Large Access: SSD -> Host Cache
void CxlSSD::handleLargeAccess(Addr pageAddr) {
    HostCacheEntry victim;
    hostCache.Insert(pageAddr, victim);
    return;
}

// Classify Node: Handle anomaly and move the dirty chunk into the dirty queue.
Tick CxlSSD::handleCNode(Addr pageAddr, Addr chunkAddr, int chunkIdx, bool isWrite) {
    bool migrate = false;
    Tick migratedLatency = 0;
    ClassifyNode* cNode = classifyQueue.Get(pageAddr);

    // Handle Anomaly
    cNode->chunk_bitmap.set(chunkIdx);
    cNode->access_counts[chunkIdx]++;
    if (cNode->chunk_bitmap.count() > thresholdDistributed) migrate = true;
    if (cNode->access_counts[chunkIdx] > thresholdIsolated) migrate = true;
    if (migrate) migratedLatency = transferPenalty4KB;

    if (isWrite) {
        moveToDirty(chunkAddr);
        cNode->dirty_bitmap.set(chunkIdx);
    }

    return migratedLatency;
}

// Store Node: Handle anomaly and move the dirty chunk into the dirty queue.
Tick CxlSSD::handleSNode(Addr chunkAddr, int chunkIdx, bool isWrite) {
    bool migrate = false;
    Tick migratedLatency = 0;
    ChunkNode* sNode = storeQueue.Get(chunkAddr);

    // Handle Anomaly
    sNode->access_count++;
    if (sNode->access_count > thresholdIsolated) migrate = true;
    if (migrate) migrateLatency = transferPenalty4KB + ssdLatency;

    if (isWrite) {
        moveToDirty(chunkAddr);
        storeQueue.Remove(chunkAddr);
    }

    return migratedLatency;
}

std::optional<std::pair<Addr, ClassifyNode>> CxlSSD::insertToClassify(Addr pageAddr, int chunkIdx) {
    ClassifyNode newNode;
    newNode.chunk_bitmap.set(chunkIdx);
    newNode.access_counts[chunkIdx] = 1;
    
    return classifyQueue.Insert(pageAddr, newNode);
}

bool CxlSSD::recvTimingReq(PacketPtr pkt) {
    Addr addr = pkt->getAddr();
    uint32_t size = pkt->getSize();
    bool isWrite = pkt->isWrite();

    Addr pageAddr = addr & ~(CXL_SSD_PAGE_SIZE - 1);
    Addr chunkAddr = addr & ~(CXL_MEM_CHUNK_SIZE - 1);
    int chunkIdx = (addr % CXL_SSD_PAGE_SIZE) / CXL_MEM_CHUNK_SIZE;
    int endIdx = ((addr + size - 1) % CXL_SSD_PAGE_SIZE) / CXL_MEM_CHUNK_SIZE;

    bool migrate = false;
    
    Tick addedLatency = -latency;

    // 1. Check Host Cache (HSPC)
    if (hostCache.Contains(pageAddr)) {
        hostCache.Remove(pageAddr);
        hostCache.Insert(pageAddr, HostCacheEntry());
        DPRINTF(CxlSSD, "Hit in Host Cache: %#x\n", addr);
        return SimpleMemory::recvTimingReq(pkt);
    }

    // 1-1. Miss in HSPC, but Cross Chunks Access -> Large IO
    if (chunkIdx != endIdx) {
        handleLargeAccess(pageAddr);
        DPRINTF(CxlSSD, "Cross Chunks Access: %#x - %#x\n", chunkIdx, endIdx);
        return SimpleMemory::recvTimingReq(pkt);
    }

    // 2. Check Device Cache (HAIPC)
    // 2-1. Check Dirty Node
    ChunkNode* dNode = dirtyQueue.Get(chunkAddr);
    if (dNode) {
        addedLatency += cxlLatency;
        pkt->headerDelay += addedLatency;
        DPRINTF(CxlSSD, "Hit in Dirty Area: %#x\n", addr);
        return SimpleMemory::recvTimingReq(pkt);
    }

    // 2-2. Check Classify Node
    ClassifyNode* cNode = classifyQueue.Get(pageAddr);
    if (cNode) {
        addedLatency += handleCNode(pageAddr, chunkAddr, chunkIdx, isWrite);
        pkt->headerDelay += addedLatency;
        DPRINTF(CxlSSD, "Hit in Classify Area: %#x\n", addr);
        return SimpleMemory::recvTimingReq(pkt);
    }

    // 2-3. Check Store Node
    ChunkNode* sNode = storeQueue.Get(chunkAddr);
    if (sNode) {
        addedLatency += handleSNode(chunkAddr, chunkIdx, isWrite);
        pkt->headerDelay += addedLatency;
        DPRINTF(CxlSSD, "Hit in Store Area: %#x\n", addr);
        return SimpleMemory::recvTimingReq(pkt);
    }

    // 3. Cache Miss (Flash Access)    
    if (pkt->getSize() > CXL_LARGE_ACCESS_THRESHOLD) {
        // Large Access: SSD -> Host Cache
        handleLargeAccess(pageAddr);
        DPRINTF(CxlSSD, "Miss (Large Access): %#x\n", addr);
    } 
    else {
        // Small Access -> Classify Area
        auto victim = insertToClassify(pageAddr, chunkIdx);
        
        // If Classify Area 
        if (victim.has_value()) {
            // 取出 Key (first) 和 Value (second)
            Addr victimAddr = victim->first;
            ClassifyNode& victimNode = victim->second;

            DPRINTF(CxlSSD, "Classify Eviction: Page %#x. Moving valid chunks to Store.\n", victimAddr);

            for (int i = 0; i < CXL_MEM_CHUNKS_PER_PAGE; i++) {
                if (victimNode.chunk_bitmap.test(i)) {
                    Addr chunkAddr = victimAddr + (i * CXL_MEM_CHUNK_SIZE);
                    
                    ChunkNode sNode;
                    sNode.access_count = victimNode.access_counts[i];
                    storeQueue.Insert(chunkAddr, sNode);
                }
            }
        }
    }
    
    addedLatency += ssdLatency;
    pkt->headerDelay += addedLatency;

    // Let SimpleMemory handle the rest
    return SimpleMemory::recvTimingReq(pkt);
}

} // namespace memory
} // namespace gem5