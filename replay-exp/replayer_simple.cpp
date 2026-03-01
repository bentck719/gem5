#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <immintrin.h>
#include <cstdint>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <replay_list.csv> [mode]" << std::endl;
        return 1;
    }

    // 0 = Baseline (Full Page Flush), 1 = Optimized (Partial Page Flush)
    bool optimized = (argc > 2 && std::string(argv[2]) == "1");
    std::string filename = argv[1];
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "Cannot Open Files" << std::endl;
        return 1;
    }

    const size_t BUFFER_SIZE = 1024 * 1024 * 1024; // 1GB
    char* ram_buffer = (char*)malloc(BUFFER_SIZE);
    
    if (!ram_buffer) {
        std::cerr << "malloc failure (stack overflow)" << std::endl;
        return 1;
    }

    for (size_t i = 0; i < BUFFER_SIZE; i += 4096) {
        ram_buffer[i] = 0;
    }

    std::cout << "--- Local DRAM Replayer ---" << std::endl;
    std::cout << "Buffer Address: " << (void*)ram_buffer << " (distributed by gem5 OS)" << std::endl;

    std::string line, header;
    std::getline(file, header); 

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string temp, syscall, size_str, addr_str;

        std::getline(ss, temp, ',');     
        std::getline(ss, syscall, ',');  
        std::getline(ss, size_str, ','); 
        std::getline(ss, addr_str, ','); 
        
        if (addr_str.empty()) continue;

        unsigned long long trace_addr = 0;
        size_t size = 0;

        try {
            trace_addr = std::stoull(addr_str, nullptr, 16); 
            size = std::stoull(size_str); 
        } catch (...) { continue; } 

        
        size_t offset = trace_addr % (BUFFER_SIZE - 4096);
        offset &= ~63; 

        char* target = ram_buffer + offset;

        if (syscall == "write" || syscall == "pwrite64") {
            for (size_t i = 0; i < size; ++i) target[i] = (char)i;
        } 
        else if (syscall == "msync" || syscall == "fsync") {
            uintptr_t start = (uintptr_t)target;
            if (optimized) {
                for (size_t i = 0; i < size; i += 64) _mm_clwb((void*)(start + i));
            } else {
                // Baseline: Flush 4KB Page
                uintptr_t page = start & ~4095;
                for (size_t i = 0; i < 4096; i += 64) _mm_clwb((void*)(page + i));
            }
            _mm_sfence();
        }
    }

    std::cout << "Complete" << std::endl;
    free(ram_buffer);
    return 0;
}