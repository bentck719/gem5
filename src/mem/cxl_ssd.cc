#include "mem/cxl_ssd.hh"
#include "debug/CxlSSD.hh"
#include "debug/CxlSSDConfig.hh"
#include "mem/cxl_controller.hh"

namespace gem5 {
namespace memory {

CxlSSD::CxlSSD(const Params &p)
    : SimpleMemory(p),
      cxlDramSize(p.cxlDramSize),
      cxlChunkSize(p.cxlChunkSize),
      ssdLatency(p.ssdLatency),
      transferPenalty4KB(p.transferPenalty4KB),
      thresholdIsolated(p.thresholdIsolated),
      thresholdDistributed(p.thresholdDistributed),
      cxlLargeAccessThreshold(p.cxlLargeAccessThreshold),
      classifyQueue(cxlDramSize / CXL_SSD_PAGE_SIZE / 2),
      storeQueue(cxlDramSize / cxlChunkSize / 4),
      dirtyQueue(cxlDramSize / cxlChunkSize / 4)
{
    DPRINTF(CxlSSDConfig, "CxlSSD Parameters:\n");
    DPRINTF(CxlSSDConfig, "  CXL DRAM Size: %llu\n", cxlDramSize);
    DPRINTF(CxlSSDConfig, "  SSD Latency: %lu\n", ssdLatency);
    DPRINTF(CxlSSDConfig, "  Threshold Isolated: %u\n", thresholdIsolated);
    DPRINTF(CxlSSDConfig, "  Threshold Distributed: %u\n", thresholdDistributed);
    DPRINTF(CxlSSDConfig, "  Large Access Threshold: %u\n", cxlLargeAccessThreshold);
}

void CxlSSD::moveToHost(Addr pageAddr) {
    // 1. Remove from HAIPC
    classifyQueue.Remove(pageAddr);
    
    for (int i=0; i<CXL_MEM_CHUNKS_PER_PAGE; i++) {
        Addr chunkAddr = pageAddr + (i * cxlChunkSize);
        storeQueue.Remove(chunkAddr);
        dirtyQueue.Remove(chunkAddr);
    }

    // The action is moved to CxlController
    // 2. Add to HSPC (Host LRU)
    // HostCacheEntry entry;
    // hostCache.Insert(pageAddr, entry);
    
    DPRINTF(CxlSSD, "Migrated Page %#x to Host Cache\n", pageAddr);
}

void CxlSSD::moveToDirty(Addr chunkAddr) {
    ChunkNode dNode;
    dirtyQueue.Insert(chunkAddr, dNode);
}

void CxlSSD::moveToStore(std::optional<std::pair<Addr, ClassifyNode>>& victim) {
    Addr victimAddr = victim->first;
    ClassifyNode& victimNode = victim->second;

    DPRINTF(CxlSSD, "Classify Eviction: Page %#x. Moving valid chunks to Store.\n", victimAddr);

    for (int i = 0; i < CXL_MEM_CHUNKS_PER_PAGE; i++) {
        // If the Chunk is written, it is in the dirty queue
        if (victimNode.dirty_bitmap.test(i)) continue;

        Addr chunkAddr = victimAddr + (i * cxlChunkSize);
        // Move Accessed Chunk to the store queue
        if (victimNode.chunk_bitmap.test(i)) {
            ChunkNode sNode;
            sNode.access_count = victimNode.access_counts[i];
            storeQueue.Insert(chunkAddr, sNode);
        }
    }
}

std::optional<std::pair<Addr, ClassifyNode>> CxlSSD::insertToClassify(Addr pageAddr, Addr chunkAddr, int chunkIdx, bool isWrite) {
    ClassifyNode newNode;
    newNode.chunk_bitmap.set(chunkIdx);
    newNode.access_counts[chunkIdx] = 1;

    if (isWrite) {
        moveToDirty(chunkAddr);
        newNode.dirty_bitmap.set(chunkIdx);
    }
    
    return classifyQueue.Insert(pageAddr, newNode);
}

// The action is moved to CxlController
// Large Access: SSD -> Host Cache
// void CxlSSD::handleLargeAccess(Addr pageAddr) {
//     HostCacheEntry victim;
//     hostCache.Insert(pageAddr, victim);
//     return;
// }

// Classify Node: Handle anomaly and move the dirty chunk into the dirty queue.
Tick CxlSSD::handleCNode(PacketPtr pkt, Addr pageAddr, Addr chunkAddr, int chunkIdx, bool isWrite) {
    bool migrate = false;
    Tick migratedLatency = 0;
    ClassifyNode* cNode = classifyQueue.Get(pageAddr);
    gem5::CXLExtraInfo* cxlInfo = dynamic_cast<gem5::CXLExtraInfo*>(pkt->senderState);

    // Handle Anomaly
    cNode->chunk_bitmap.set(chunkIdx);
    cNode->access_counts[chunkIdx]++;
    if (cNode->chunk_bitmap.count() > thresholdDistributed) migrate = true;
    if (cNode->access_counts[chunkIdx] > thresholdIsolated) migrate = true;

    if (migrate) {
        moveToHost(pageAddr);
        migratedLatency = transferPenalty4KB;
        cxlInfo->needMigration = true;
    }
    else if (isWrite) {
        moveToDirty(chunkAddr);
        cNode->dirty_bitmap.set(chunkIdx);
    }

    return migratedLatency;
}

// Store Node: Handle anomaly and move the dirty chunk into the dirty queue.
Tick CxlSSD::handleSNode(PacketPtr pkt, Addr chunkAddr, int chunkIdx, bool isWrite) {
    Tick migratedLatency = 0;
    ChunkNode* sNode = storeQueue.Get(chunkAddr);
    CXLExtraInfo* cxlInfo = dynamic_cast<gem5::CXLExtraInfo*>(pkt->senderState);

    // Handle Anomaly
    sNode->access_count++;

    if (sNode->access_count > thresholdIsolated) {
        Addr pageAddr = chunkAddr & ~(CXL_SSD_PAGE_SIZE - 1);
        moveToHost(pageAddr);
        migratedLatency = transferPenalty4KB + ssdLatency;
        cxlInfo->needMigration = true;

    } 
    else if (isWrite) {
        moveToDirty(chunkAddr);
        storeQueue.Remove(chunkAddr);
    }

    return migratedLatency;
}

bool CxlSSD::recvTimingReq(PacketPtr pkt) {
    Addr addr = pkt->getAddr();
    uint32_t size = pkt->getSize();
    bool isWrite = pkt->isWrite();

    Addr pageAddr = addr & ~(CXL_SSD_PAGE_SIZE - 1);
    Addr pageAddrEnd = (addr + size - 1) & ~(CXL_SSD_PAGE_SIZE - 1);
    Addr chunkAddr = addr & ~(cxlChunkSize - 1);
    Addr chunkAddrEnd = (addr + size - 1) & ~(cxlChunkSize - 1);
    int chunkIdx = (addr % CXL_SSD_PAGE_SIZE) / cxlChunkSize;
    
    Tick addedLatency = 0;

    // Miss in HSPC, but Cross Chunks Access -> Large IO
    if (pageAddr != pageAddrEnd || chunkAddr != chunkAddrEnd) {
        for (Addr iterAddr = pageAddr; iterAddr <= pageAddrEnd; iterAddr += CXL_SSD_PAGE_SIZE) {
            // The action is moved to CxlController
            // handleLargeAccess(iterAddr);
            moveToHost(iterAddr);
            addedLatency += ssdLatency;
            addedLatency += transferPenalty4KB;
        }
        pkt->headerDelay += addedLatency;
        DPRINTF(CxlSSD, "Cross Pages/Chunks Access: Page Addr: %#llx - %#llx, Chunk Addr: %#llx - %#llx\n", pageAddr, pageAddrEnd, chunkAddr, chunkAddrEnd);
        return SimpleMemory::recvTimingReq(pkt);
    }

    // Check Dirty Node
    ChunkNode* dNode = dirtyQueue.Get(chunkAddr);
    if (dNode) {
        DPRINTF(CxlSSD, "Hit in Dirty Area: %#x\n", addr);
        return SimpleMemory::recvTimingReq(pkt);
    }

    // The action is moved to CxlController
    // Check Host Cache (HSPC)

    // Check Device Cache (HAIPC)
    // Check Classify Node
    ClassifyNode* cNode = classifyQueue.Get(pageAddr);
    if (cNode) {
        addedLatency += handleCNode(pkt, pageAddr, chunkAddr, chunkIdx, isWrite);
        pkt->headerDelay += addedLatency;
        DPRINTF(CxlSSD, "Hit in Classify Area: %#x\n", addr);
        return SimpleMemory::recvTimingReq(pkt);
    }

    // Check Store Node
    ChunkNode* sNode = storeQueue.Get(chunkAddr);
    if (sNode) {
        addedLatency += handleSNode(pkt, chunkAddr, chunkIdx, isWrite);
        pkt->headerDelay += addedLatency;
        DPRINTF(CxlSSD, "Hit in Store Area: %#x\n", addr);
        return SimpleMemory::recvTimingReq(pkt);
    }

    // Cache Miss (Flash Access)
    addedLatency += ssdLatency;

    // Large Access: SSD -> Host Cache
    if (size > cxlLargeAccessThreshold) {
        // The action is moved to CxlController
        // handleLargeAccess(pageAddr);
        addedLatency += transferPenalty4KB;
        pkt->headerDelay += addedLatency;
        DPRINTF(CxlSSD, "Miss (Large Access) Latency: %#llu\n", addedLatency);
        return SimpleMemory::recvTimingReq(pkt);
    } 
    
    // Small Access -> Classify Area
    std::optional<std::pair<Addr, ClassifyNode>> victim = insertToClassify(pageAddr, chunkAddr, chunkIdx, isWrite);
    
    // Classify Full -> Move to Store Area
    if (victim.has_value()) {
        moveToStore(victim);
        DPRINTF(CxlSSD, "Miss (Move to Store): %#x\n", addr);
    }

    DPRINTF(CxlSSD, "Miss (Small Access): %#x\n", addr);
    
    pkt->headerDelay += addedLatency;

    return SimpleMemory::recvTimingReq(pkt);
}

} // namespace memory
} // namespace gem5