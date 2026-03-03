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

size_t global_full_page_flushes = 0;
size_t global_partial_clwbs = 0;

struct PageTracker {
    // 64 bits 對應 4KB Page 中的 64 條 64-Byte Cache Lines
    uint64_t dirty_bitmap = 0; 

    void add_range(uintptr_t start, uintptr_t end, uintptr_t page_base) {
        // 計算在這個 4KB 頁面內的偏移量
        uintptr_t start_offset = start - page_base;
        uintptr_t end_offset = end - page_base;
        
        // 轉換為 Cache Line 索引 (0 ~ 63)
        int start_line = start_offset / 64;
        int end_line = (end_offset + 63) / 64; 
        if (end_line > 64) end_line = 64; // 安全邊界保護

        // 標記這段範圍的 Cache Lines 為 Dirty
        for (int i = start_line; i < end_line; ++i) {
            dirty_bitmap |= (1ULL << i);
        }
    }
};

void handleWrite(std::map<uintptr_t, PageTracker>& page_tracker, volatile char *target_ptr, 
    size_t write_len, uintptr_t start_addr, uintptr_t end_addr) {
    
    // A. Hardware Simulation: Trigger cache line traffic
    for (size_t i = 0; i < write_len; i += 64) {
        target_ptr[i] = 0xAA;
    }

    // B. Kernel Tracking Logic (極輕量化)
    if (write_len > 0) {
        uintptr_t curr = start_addr;
        
        // Handle Cross-Page Writes
        while (curr <= end_addr) {
            uintptr_t page_base = curr & ~4095;
            uintptr_t next_page_boundary = page_base + 4096;
            uintptr_t chunk_end = std::min(end_addr, next_page_boundary);
            
            page_tracker[page_base].add_range(curr, chunk_end, page_base);
            curr = chunk_end; // Move to the next page chunk
        }
    }
}

void handleSync(std::map<uintptr_t, PageTracker>& page_tracker) {
    if (page_tracker.empty()) return;
    
    for (auto it = page_tracker.begin(); it != page_tracker.end(); ) {
        uintptr_t curr_page = it->first;
        uint64_t bitmap = it->second.dirty_bitmap;

        if (optimized) {
            // Optimized 模式：使用 Bitmap 與 Threshold
            int dirty_lines = __builtin_popcountll(bitmap);

            if (dirty_lines >= 48) { // 3KB Threshold
                for (uintptr_t p = curr_page; p < curr_page + 4096; p += 64) {
                    _mm_clwb((void*)p);
                }
                global_full_page_flushes++;
            } 
            else {
                for (int i = 0; i < 64; ++i) {
                    if (bitmap & (1ULL << i)) {
                        _mm_clwb((void*)(curr_page + i * 64));
                        global_partial_clwbs++;
                    }
                }
            }
        } 
        else {
            // Baseline 模式：整頁刷寫 (模擬 Linux 預設對 Dirty Page 的行為)
            for (uintptr_t p = curr_page; p < curr_page + 4096; p += 64) {
                _mm_clwb((void*)p);
            }
            global_full_page_flushes++;
        }
        
        // 同步完後將該頁面從 tracker 移除
        it = page_tracker.erase(it);
    }
    _mm_sfence(); // Ensure all clwb instructions are globally visible
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
    std::cout << "Mode: " << (optimized ? "Optimized (Bitmap Tracker with 3KB Threshold)" : "Baseline (Full Page Flush)") << std::endl;
    
    // First layer: Key is Page Address (4KB aligned), Value is the PageTracker (64-bit Bitmap)
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
                // std::cerr << "[Warning] Skip line (Size too large): " << line << std::endl;
                continue;
            }
        } catch (const std::exception& e) {
            // std::cerr << "[Warning] Skip line (Unrecognized size): " << line << std::endl;
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
            std::cout << syscall << std::endl;
            handleSync(page_tracker);
        }
    }

    std::cout << "\nReplay task completed successfully." << std::endl;
    
    // 輸出最終統計結果
    if (optimized) {
        std::cout << "========================================" << std::endl;
        std::cout << "[Optimized Mode Summary]" << std::endl;
        std::cout << "1. Full Page Flushes (>= 3KB) : " << global_full_page_flushes << " pages" << std::endl;
        std::cout << "2. Partial CLWBs (< 3KB)      : " << global_partial_clwbs << " cache lines" << std::endl;
        std::cout << "========================================" << std::endl;
    }
    else {
        std::cout << "========================================" << std::endl;
        std::cout << "[Baseline Mode Summary]" << std::endl;
        std::cout << "1. Full Page Flushes: " << global_full_page_flushes << " pages" << std::endl;
        std::cout << "========================================" << std::endl;
    }

    free(sim_mem);
    return 0;
}