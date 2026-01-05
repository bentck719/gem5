#include "mem/cxl_ssd.hh"
#include "debug/CxlSSD.hh"
#include "debug/CxlSSDConfig.hh"

namespace gem5 {
namespace memory {

CxlSSD::CxlSSD(const Params &p)
    : SimpleMemory(p),
      stats(this),
      cxlLatency(p.cxl_latency),
      cxlBandwidth(p.cxl_bandwidth),
      cxlDramSize(p.cxl_dram_size),
      ssdLatency(p.ssd_latency),
      transferPenalty4KB(p.transfer_penalty4KB),
      hostLatency(p.host_latency),
      hostDramSize(p.host_dram_size),
      thresholdIsolated(p.threshold_isolated),
      thresholdDistributed(p.threshold_distributed),
      cxlLargeAccessThreshold(p.cxl_large_access_threshold),
      classifyQueue(cxlDramSize / CXL_SSD_PAGE_SIZE / 2),
      storeQueue(cxlDramSize / CXL_MEM_CHUNK_SIZE / 4),
      dirtyQueue(cxlDramSize / CXL_MEM_CHUNK_SIZE / 4),
      hostCache(hostDramSize / CXL_SSD_PAGE_SIZE)
{
    DPRINTF(CxlSSDConfig, "CxlSSD Parameters:\n");
    DPRINTF(CxlSSDConfig, "  CXL Latency: %lu\n", cxlLatency);
    DPRINTF(CxlSSDConfig, "  CXL Bandwidth: %llf\n", cxlBandwidth);
    DPRINTF(CxlSSDConfig, "  CXL DRAM Size: %llu\n", cxlDramSize);
    DPRINTF(CxlSSDConfig, "  SSD Latency: %lu\n", ssdLatency);
    DPRINTF(CxlSSDConfig, "  Host Latency: %lu\n", hostLatency);
    DPRINTF(CxlSSDConfig, "  Host DRAM Size: %llu\n", hostDramSize);
    DPRINTF(CxlSSDConfig, "  Threshold Isolated: %u\n", thresholdIsolated);
    DPRINTF(CxlSSDConfig, "  Threshold Distributed: %u\n", thresholdDistributed);
    DPRINTF(CxlSSDConfig, "  Large Access Threshold: %u\n", cxlLargeAccessThreshold);
}

CxlSSD::CxlStats::CxlStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(readHitsHost, "Number of read hits in Host Cache"),
      ADD_STAT(readHitsClassify, "Number of read hits in Classify"),
      ADD_STAT(readHitsStore, "Number of read hits in Store"),
      ADD_STAT(readHitsDirty, "Number of read hits in Dirty"),
      ADD_STAT(readMisses, "Number of read misses (Flash access)"),
      ADD_STAT(largeAccesses, "Number of large accesses redirected to Host"),
      ADD_STAT(smallAccesses, "Number of small accesses redirected to Host"),
      ADD_STAT(crossPageAccesses, "Number of cross accesses redirected to Host"),
      ADD_STAT(migrationFromCNode, "Number of pages migrated from C to Host"),
      ADD_STAT(migrationFromSNode, "Number of pages migrated from S to Host"),

      ADD_STAT(totalLatency, "Total latency incurred by CxlSSD"),
      ADD_STAT(migrationFromCNodeLatency, "Total latency incurred by Migration from C"),
      ADD_STAT(migrationFromSNodeLatency, "Total latency incurred by Migration from S"),
      ADD_STAT(hitsHostLatency, "Total latency incurred by hitting host"),
      ADD_STAT(hitsClassifyLatency, "Total latency incurred by hitting classify"),
      ADD_STAT(hitsStoreLatency, "Total latency incurred by hitting store"),
      ADD_STAT(hitsDirtyLatency, "Total latency incurred by hitting dirty"),
      ADD_STAT(largeAccessLatency, "Total latency incurred by large access"),
      ADD_STAT(smallAccessLatency, "Total latency incurred by small access"),
      ADD_STAT(ssdAccessLatency, "Total latency incurred by ssd access"),
      ADD_STAT(blockIOLatency, "Total latency incurred by block io"),

      ADD_STAT(latencyDistribution, "Latency distribution")
{
    latencyDistribution
        .init(32)
        .flags(statistics::pdf | statistics::nozero); 
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

        Addr chunkAddr = victimAddr + (i * CXL_MEM_CHUNK_SIZE);
        // Move Accessed Chunk to the store queue
        if (victimNode.chunk_bitmap.test(i)) {
            ChunkNode sNode;
            sNode.access_count = victimNode.access_counts[i];
            storeQueue.Insert(chunkAddr, sNode);
        }
    }
}

std::optional<std::pair<Addr, ClassifyNode>> CxlSSD::insertToClassify(const AddrPacket& info, bool isWrite) {
    Addr pageAddr = info.pageAddr;
    Addr chunkAddr = info.chunkAddr;
    int chunkIdx = info.chunkIdx;
    ClassifyNode newNode;
    newNode.chunk_bitmap.set(chunkIdx);
    newNode.access_counts[chunkIdx] = 1;

    if (isWrite) {
        moveToDirty(chunkAddr);
        newNode.dirty_bitmap.set(chunkIdx);
    }
    
    return classifyQueue.Insert(pageAddr, newNode);
}

// Large Access: SSD -> Host Cache
void CxlSSD::handleLargeAccess(Addr pageAddr) {
    moveToHost(pageAddr);
    return;
}

// Classify Node: Handle anomaly and move the dirty chunk into the dirty queue.
Tick CxlSSD::handleCNode(const AddrPacket& info, bool isWrite) {
    Addr pageAddr = info.pageAddr;
    Addr chunkAddr = info.chunkAddr;
    int chunkIdx = info.chunkIdx;
    bool migrate = false;
    ClassifyNode* cNode = classifyQueue.Get(pageAddr);

    // Handle Anomaly
    cNode->chunk_bitmap.set(chunkIdx);
    cNode->access_counts[chunkIdx]++;
    if (cNode->chunk_bitmap.count() > thresholdDistributed) migrate = true;
    if (cNode->access_counts[chunkIdx] > thresholdIsolated) migrate = true;

    if (migrate) {
        stats.migrationFromCNode++;

        moveToHost(pageAddr);
        
        stats.migrationFromCNodeLatency += transferPenalty4KB;
        stats.blockIOLatency += transferPenalty4KB;
    }
    else if (isWrite) {
        moveToDirty(chunkAddr);
        cNode->dirty_bitmap.set(chunkIdx);
    }

    return cxlLatency;
}

// Store Node: Handle anomaly and move the dirty chunk into the dirty queue.
Tick CxlSSD::handleSNode(const AddrPacket& info, bool isWrite) {
    Addr chunkAddr = info.chunkAddr;
    ChunkNode* sNode = storeQueue.Get(chunkAddr);

    // Handle Anomaly
    sNode->access_count++;
    if (sNode->access_count > thresholdIsolated) {
        stats.migrationFromSNode++;

        Addr pageAddr = chunkAddr & ~(CXL_SSD_PAGE_SIZE - 1);
        moveToHost(pageAddr);

        stats.migrationFromSNodeLatency += transferPenalty4KB + ssdLatency;
        stats.ssdAccessLatency += ssdLatency;
        stats.blockIOLatency += transferPenalty4KB;
    } 
    else if (isWrite) {
        moveToDirty(chunkAddr);
        storeQueue.Remove(chunkAddr);
    }

    return cxlLatency;
}

bool CxlSSD::recvTimingReq(PacketPtr pkt) {
    Addr addr = pkt->getAddr();
    bool isWrite = pkt->isWrite();

    AddrPacket addrInfo(pkt);
    Addr pageAddr = addrInfo.pageAddr;
    Addr pageAddrEnd = addrInfo.pageAddrEnd;
    Addr chunkAddr = addrInfo.chunkAddr;
    Addr chunkAddrEnd = addrInfo.chunkAddrEnd;
    
    Tick addedLatency = 0;
    
    // Cross Pages/Chunks Access -> Large IO
    if (addrInfo.isLargeAccess()) {
        stats.crossPageAccesses++;
        for (Addr iterAddr = pageAddr; iterAddr <= pageAddrEnd; iterAddr += CXL_SSD_PAGE_SIZE) {
            // Check Host Cache (HSPC)
            if (hostCache.Contains(iterAddr)) {
                stats.readHitsHost++;
                hostCache.Remove(iterAddr);
                hostCache.Insert(iterAddr, HostCacheEntry());
                stats.hitsHostLatency += hostLatency;
                stats.totalLatency += hostLatency;
            }
            else {
                stats.readMisses++;
                stats.largeAccesses++;

                handleLargeAccess(iterAddr);
                addedLatency += ssdLatency;
                addedLatency += transferPenalty4KB;

                stats.ssdAccessLatency += ssdLatency;
                stats.blockIOLatency += transferPenalty4KB;

                stats.largeAccessLatency += addedLatency;
                stats.totalLatency += addedLatency;
            }
        }
        pkt->headerDelay += addedLatency;
        stats.latencyDistribution.sample(addedLatency);
        DPRINTF(CxlSSD, "Cross Pages/Chunks Access: Page Addr: %#llx - %#llx, Chunk Addr: %#llx - %#llx\n", pageAddr, pageAddrEnd, chunkAddr, chunkAddrEnd);
        return SimpleMemory::recvTimingReq(pkt);
    }

    // Check Dirty Node
    ChunkNode* dNode = dirtyQueue.Get(chunkAddr);
    if (dNode) {
        stats.readHitsDirty++;

        addedLatency += cxlLatency;
        DPRINTF(CxlSSD, "Hit in Dirty Area: %#x\n", addr);
        pkt->headerDelay += addedLatency;

        stats.totalLatency += addedLatency;
        stats.hitsDirtyLatency += addedLatency;
        stats.latencyDistribution.sample(addedLatency);

        return SimpleMemory::recvTimingReq(pkt);
    }

    // Check Host Cache (HSPC)
    if (hostCache.Contains(pageAddr)) {
        stats.readHitsHost++;

        hostCache.Promote(pageAddr);
        DPRINTF(CxlSSD, "Hit in Host Cache: %#x\n", addr);

        stats.totalLatency += hostLatency;
        stats.hitsHostLatency += hostLatency;
        stats.latencyDistribution.sample(hostLatency);

        return SimpleMemory::recvTimingReq(pkt);
    }

    // Check Device Cache (HAIPC)
    // Check Classify Node
    ClassifyNode* cNode = classifyQueue.Get(pageAddr);
    if (cNode) {
        stats.readHitsClassify++;

        addedLatency += handleCNode(addrInfo, isWrite);
        pkt->headerDelay += addedLatency;
        DPRINTF(CxlSSD, "Hit in Classify Area: %#x\n", addr);

        stats.totalLatency += addedLatency;
        stats.hitsClassifyLatency += addedLatency;
        stats.latencyDistribution.sample(addedLatency);

        return SimpleMemory::recvTimingReq(pkt);
    }

    // Check Store Node
    ChunkNode* sNode = storeQueue.Get(chunkAddr);
    if (sNode) {
        stats.readHitsStore++;

        addedLatency += handleSNode(addrInfo, isWrite);
        pkt->headerDelay += addedLatency;
        DPRINTF(CxlSSD, "Hit in Store Area: %#x\n", addr);

        stats.totalLatency += addedLatency;
        stats.hitsStoreLatency += addedLatency;
        stats.latencyDistribution.sample(addedLatency);

        return SimpleMemory::recvTimingReq(pkt);
    }

    // Cache Miss (Flash Access)
    stats.readMisses++;
    
    // Large Access: SSD -> Host Cache
    if (pkt->getSize() > cxlLargeAccessThreshold) {
        stats.largeAccesses++;

        handleLargeAccess(pageAddr);
        // fetch data from ssd and transfer data through block I/O
        addedLatency += ssdLatency;
        addedLatency += transferPenalty4KB;

        pkt->headerDelay += addedLatency;
        DPRINTF(CxlSSD, "Miss (Large Access): %#x\n", addr);

        stats.ssdAccessLatency += ssdLatency;
        stats.blockIOLatency += transferPenalty4KB;
        stats.totalLatency += addedLatency;
        stats.largeAccessLatency += addedLatency;
        stats.latencyDistribution.sample(addedLatency);

        return SimpleMemory::recvTimingReq(pkt);
    } 
    
    // Small Access -> Classify Area
    std::optional<std::pair<Addr, ClassifyNode>> victim = insertToClassify(addrInfo, isWrite);
    stats.smallAccesses++;
    
    // Classify Full -> Move to Store Area
    if (victim.has_value()) {
        moveToStore(victim);
        DPRINTF(CxlSSD, "Miss (Move to Store): %#x\n", addr);
    }
    
    DPRINTF(CxlSSD, "Miss (Small Access): %#x\n", addr);
    
    addedLatency += ssdLatency;
    addedLatency += cxlLatency;
    pkt->headerDelay += addedLatency;

    stats.ssdAccessLatency += ssdLatency;
    stats.totalLatency += addedLatency;
    stats.smallAccessLatency += addedLatency;
    stats.latencyDistribution.sample(addedLatency);

    return SimpleMemory::recvTimingReq(pkt);
}

} // namespace memory
} // namespace gem5