#include <map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <immintrin.h> 
#include <cstdint>
#include <algorithm>
#include <sys/mman.h>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <cstdlib>

const size_t SIM_MEM_SIZE = (size_t)8 * 1024 * 1024 * 1024;
bool optimized = false;

struct PageTracker {
    size_t total_dirty_size = 0;
    bool full_page_dirty = false;
    // Key: Start Address, Value: End Address (Maintains non-overlapping intervals)
    std::map<uintptr_t, uintptr_t> ranges;

    void add_range(uintptr_t start, uintptr_t end) {
        if (full_page_dirty) return;

        // 1. Find the first interval that might overlap (starts >= start)
        auto it = ranges.lower_bound(start);

        // 2. Check if the previous interval overlaps or is adjacent (Merge with previous)
        if (it != ranges.begin()) {
            auto prev = std::prev(it);
            if (prev->second >= start) {
                start = prev->first;
                end = std::max(end, prev->second);
                ranges.erase(prev);
            }
        }

        // 3. Merge with all subsequent overlapping intervals
        while (it != ranges.end() && it->first <= end) {
            end = std::max(end, it->second);
            it = ranges.erase(it);
        }

        // 4. Insert the final merged interval
        ranges[start] = end;

        // 5. Update total dirty size and check the 3KB (3072 Bytes) threshold
        total_dirty_size = 0;
        for (auto const& [s, e] : ranges) {
            total_dirty_size += (e - s);
        }

        if (total_dirty_size > 3072) {
            full_page_dirty = true;
            ranges.clear();
        }
    }
};

void handleWrite(std::map<uintptr_t, PageTracker>& page_tracker, volatile char *target_ptr, 
    size_t write_len, uintptr_t start_addr, uintptr_t end_addr) {
    // A. Hardware Simulation: Trigger cache line traffic
    for (size_t i = 0; i < write_len; i += 64) {
        target_ptr[i] = 0xAA;
    }

    // B. Kernel Tracking Logic
    if (optimized && write_len > 0) {
        uintptr_t curr = start_addr;
        
        // Handle Cross-Page Writes
        while (curr < end_addr) {
            uintptr_t page_base = curr & ~4095;
            uintptr_t next_page_boundary = page_base + 4096;
            uintptr_t chunk_end = std::min(end_addr, next_page_boundary);
            
            page_tracker[page_base].add_range(curr, chunk_end);
            
            curr = chunk_end; // Move to the next page chunk
        }
    }
}

void handleSync(std::map<uintptr_t, PageTracker>& page_tracker, uintptr_t start_flush, uintptr_t end_flush) {
    uintptr_t curr_page = start_flush & ~4095;
    size_t total_nodes_flushed = 0;

    // Iterate through all pages covered by the sync command
    while (curr_page < end_flush) {
        if (optimized) {
            if (page_tracker.count(curr_page)) {
                auto& pt = page_tracker[curr_page];
                
                if (pt.full_page_dirty) {
                    // Degraded mode: flush entire 4KB page (64 clwb)
                    for (uintptr_t p = curr_page; p < curr_page + 4096; p += 64) {
                        _mm_clwb((void*)p);
                    }
                } 
                else {
                    // Optimized mode: flush only the recorded dirty words
                    total_nodes_flushed += pt.ranges.size();
                    for (auto const& [s, e] : pt.ranges) {
                        uintptr_t flush_start = s & ~63; // Align to 64B
                        for (uintptr_t p = flush_start; p < e; p += 64) {
                            _mm_clwb((void*)p);
                        }
                    }
                }
                // Clear tracking after sync
                page_tracker.erase(curr_page);
            }
        } 
        else {
            // Baseline mode: always flush entire 4KB page
            for (uintptr_t p = curr_page; p < curr_page + 4096; p += 64) {
                _mm_clwb((void*)p);
            }
        }
        curr_page += 4096; // Move to the next page
    }
    _mm_sfence(); // Ensure all clwb instructions are globally visible

    if (optimized && total_nodes_flushed > 0) {
        // Node size is roughly 32-48 bytes on 64-bit systems
        size_t node_size = sizeof(std::_Rb_tree_node_base) + (sizeof(uintptr_t) * 2); 
        size_t metadata_dram_cost = total_nodes_flushed * node_size;
        
        std::cout << "[Optimized Stats] Sync triggered. RB-Tree nodes: " << total_nodes_flushed 
                  << ", DRAM consumption: " << metadata_dram_cost << " Bytes" << std::endl;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <replay_list.csv> [use_optimized_flush(1/0)]" << std::endl;
        return 1;
    }

    optimized = (argc > 2 && std::string(argv[2]) == "1");
    std::string filename = argv[1];
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << filename << std::endl;
        return 1;
    }

    // 1. Define CXL Memory Space
    char* sim_mem = (char*)aligned_alloc(4096, SIM_MEM_SIZE);
    if (!sim_mem) {
        std::cerr << "[Fatal Error] Memory allocation failed!" << std::endl;
        return 1;
    }

    // Touch pages to allocate physical memory
    for (size_t i = 0; i < SIM_MEM_SIZE; i += 4096) {
        sim_mem[i] = 0;
    }

    std::string line, header;
    std::getline(file, header); 

    std::cout << "--- gem5 CXL Replayer ---" << std::endl;
    std::cout << "Target CXL Address: " << (void*)sim_mem << std::endl; 
    std::cout << "Mode: " << (optimized ? "Optimized (Hierarchical RB-Tree with 3KB Threshold)" : "Baseline (Full Page Flush)") << std::endl;
    // First layer: Key is Page Address (4KB aligned), Value is the PageTracker (Sub-tree)
    std::map<uintptr_t, PageTracker> page_tracker;

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string ts, syscall, id_addr_str, size_str, offset_str;

        std::getline(ss, ts, ',');
        std::getline(ss, syscall, ',');
        std::getline(ss, id_addr_str, ',');
        std::getline(ss, size_str, ',');
        std::getline(ss, offset_str, ',');

        if (id_addr_str.empty() || id_addr_str == "0") continue;

        uintptr_t current_trace_addr = 0;
        try {
            if (syscall == "mmap" || syscall == "munmap" || syscall == "msync") {
                current_trace_addr = std::stoull(id_addr_str, nullptr, 16);
            } 
            else if (syscall == "pread64" || syscall == "pwrite64") {
                current_trace_addr = offset_str.empty() ? 0 : std::stoull(offset_str);
            } 
            else {
                static size_t seq_off = 0;
                current_trace_addr = (std::stoull(id_addr_str) * 0x1000000) + seq_off;
                seq_off += 4096;
            }
        } catch (...) { continue; }

        size_t size = 0;
        try {
            size = std::stoull(size_str);
            if (size > SIM_MEM_SIZE) {
                std::cerr << "[Warning] Skip line (Size too large): " << line << std::endl;
                continue;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Warning] Skip line (Unrecognized size): " << line << std::endl;
            continue; 
        }

        // Calculate relative offset and prevent out-of-bounds
        size_t rel_offset = current_trace_addr % (SIM_MEM_SIZE - 4096);
        volatile char* target_ptr = (volatile char*)(sim_mem + rel_offset);

        if (syscall == "write" || syscall == "pwrite64") {
            size_t write_len = std::min(size, SIM_MEM_SIZE - rel_offset);
            uintptr_t start_addr = (uintptr_t)target_ptr;
            uintptr_t end_addr = start_addr + write_len;
            
            handleWrite(page_tracker, target_ptr, write_len, start_addr, end_addr);
        }
        else if (syscall == "read" || syscall == "pread64") {
            size_t read_len = std::min(size, SIM_MEM_SIZE - rel_offset);
            
            if (read_len > 0) {
                volatile char dummy_read; 
                // Read once per 64 bytes to pull the cache line
                for (size_t i = 0; i < read_len; i += 64) {
                    dummy_read = target_ptr[i];
                }
                if (read_len % 64 != 0) {
                    dummy_read = target_ptr[read_len - 1];
                }
                (void)dummy_read; // Prevent unused variable warning
            }
        }
        else if (syscall == "msync" || syscall == "fsync" || syscall == "fdatasync") {
            size_t flush_len = std::min(size, SIM_MEM_SIZE - rel_offset);
            uintptr_t start_flush = (uintptr_t)target_ptr;
            uintptr_t end_flush = start_flush + flush_len;
            
            handleSync(page_tracker, start_flush, end_flush);
        }
    }

    std::cout << "Replay task completed successfully." << std::endl;
    free(sim_mem);
    return 0;
}
