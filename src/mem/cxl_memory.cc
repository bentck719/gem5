#include "mem/cxl_memory.hh"
#include "debug/CxlMemory.hh"
#include "debug/CxlMemoryConfig.hh"

namespace gem5 {
namespace memory {

CxlMemory::CxlMemory(const Params &p)
    : SimpleMemory(p),
      cxlLatency(p.cxl_latency),
      hostLatency(p.host_latency),
      ssdReadLatency(p.ssd_read_latency),
      ssdWriteLatency(p.ssd_write_latency),
      transferPenalty(p.transfer_penalty4KB),
      cxlDramSize(p.cxl_dram_size),
      hostDramSize(p.host_dram_size),
      hostQueue(hostDramSize / PAGE_SIZE),
      cxlActiveQueue((cxlDramSize / PAGE_SIZE) / 2),
      cxlInactiveQueue((cxlDramSize / PAGE_SIZE) / 2),
      stats(*this)
{
}

CxlMemory::CxlMemoryStats::CxlMemoryStats(CxlMemory &cxl_mem)
    : statistics::Group(&cxl_mem),
      ADD_STAT(statHostHits, statistics::units::Count::get(), "Number of hits in Host DRAM"),
      ADD_STAT(statCxlActiveHits, statistics::units::Count::get(), "Hits in CXL Active LRU"),
      ADD_STAT(statCxlInactiveHits, statistics::units::Count::get(), "Hits in CXL Inactive LRU"),
      ADD_STAT(statSsdMisses, statistics::units::Count::get(), "Number of misses going to SSD"),
      ADD_STAT(statCleanEvicts, statistics::units::Count::get(), "Number of clean evictions"),
      ADD_STAT(statWritebacks, statistics::units::Count::get(), "Number of dirty writebacks to SSD")
{
}

bool CxlMemory::recvTimingReq(PacketPtr pkt) {
    panic_if(pkt->cacheResponding(), "Should not see packets where cache is responding");
    panic_if(!(pkt->isRead() || pkt->isWrite()), "Should only see read and writes");

    if (retryReq) return false;
    if (isBusy) {
        retryReq = true;
        return false;
    }

    Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
    pkt->headerDelay = pkt->payloadDelay = 0;

    Tick duration = pkt->getSize() * bandwidth;
    if (duration != 0) {
        schedule(releaseEvent, curTick() + duration);
        isBusy = true;
    }

    bool needsResponse = pkt->needsResponse();
    recvAtomic(pkt);

    Addr pageAddr = pkt->getAddr() & ~(PAGE_SIZE - 1);
    Tick addedLatency = 0;

    if (needsResponse) {
        assert(pkt->isResponse());

        Addr pageAddr = pkt->getAddr() & ~(PAGE_SIZE - 1);
        bool isWrite = pkt->isWrite() || (pkt->cmd == MemCmd::WritebackDirty);
        Tick dynamicLatency = 0;

        if (pkt->cmd == MemCmd::WritebackDirty) stats.statWritebacks++;
        else if (pkt->cmd == MemCmd::CleanEvict) stats.statCleanEvicts++;

        auto DemoteToInactiveCXL = [&](Addr demoteAddr, bool demoteDirty) {
            auto inactiveVictim = cxlInactiveQueue.Insert(demoteAddr, { .isDirty = demoteDirty });
            if (inactiveVictim.has_value() && inactiveVictim->second.isDirty) {
                dynamicLatency += transferPenalty + ssdWriteLatency;
                stats.statWritebacks++;
            }
        };

        // 1. Host DRAM Hit
        if (hostQueue.Contains(pageAddr)) {
            stats.statHostHits++;
            hostQueue.Promote(pageAddr);
            dynamicLatency = hostLatency;
            if (isWrite) hostQueue.Get(pageAddr)->isDirty = true;
            DPRINTF(CxlMemory, "Hit in Host: %#x\n", pkt->getAddr());
        }
        // 2. CXL Active LRU Hit (觸發晉升 Promotion 到 Host)
        else if (cxlActiveQueue.Contains(pageAddr)) {
            stats.statCxlActiveHits++;
            
            // 從 CXL Active 取出並放入 Host
            bool dirty = cxlActiveQueue.Get(pageAddr)->isDirty || isWrite;
            cxlActiveQueue.Remove(pageAddr);
            auto hostVictim = hostQueue.Insert(pageAddr, { .isDirty = dirty });
            
            // 負擔從 CXL 搬移到 Host 的延遲
            dynamicLatency = cxlLatency + transferPenalty; 
            
            // 如果 Host 滿了，把 Host 的冷資料降級到 Inactive CXL
            if (hostVictim.has_value()) {
                DemoteToInactiveCXL(hostVictim->first, hostVictim->second.isDirty);
            }
        }
        // 3. CXL Inactive LRU Hit (觸發活化到 Active，但不進 Host)
        else if (cxlInactiveQueue.Contains(pageAddr)) {
            stats.statCxlInactiveHits++;
            
            bool dirty = cxlInactiveQueue.Get(pageAddr)->isDirty || isWrite;
            cxlInactiveQueue.Remove(pageAddr);
            auto activeVictim = cxlActiveQueue.Insert(pageAddr, { .isDirty = dirty });
            
            // CXL 內部移動，延遲較小
            dynamicLatency = cxlLatency; 
            
            // 如果 Active 滿了，把 Active 裡最冷的退回 Inactive
            if (activeVictim.has_value()) {
                DemoteToInactiveCXL(activeVictim->first, activeVictim->second.isDirty);
            }
        }
        // 4. SSD Miss (直接載入 Host，因為 CPU 馬上要用)
        else {
            stats.statSsdMisses++;
            
            dynamicLatency = ssdReadLatency + transferPenalty + hostLatency; 
            if (isWrite) {
                dynamicLatency += transferPenalty + ssdWriteLatency;
            }

            auto hostVictim = hostQueue.Insert(pageAddr, { .isDirty = isWrite });
            
            // 發生一連串的降級：Host 滿了踢給 CXL，CXL 滿了下刷 SSD
            if (hostVictim.has_value()) {
                DemoteToInactiveCXL(hostVictim->first, hostVictim->second.isDirty);
            }
        }

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
    } else {
        pendingDelete.reset(pkt);
    }

    return true;
}

} // namespace memory
} // namespace gem5