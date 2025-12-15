#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <gem5/m5ops.h> // 需要在編譯時連結 gem5 library

#define PAGE_SIZE 4096
#define CHUNK_SIZE 256

// 讀取特定地址的 helper function
// 使用 volatile 確保編譯器不會把 memory access 優化掉
void touch_addr(volatile char *addr) {
    volatile char val = *addr;
}

void write_addr(volatile char *addr) {
    *addr = 'A';
}

int main() {
    // 分配一大塊記憶體，確保跨越多個 Page
    char *memory = (char *)aligned_alloc(PAGE_SIZE, PAGE_SIZE * 10);
    memset(memory, 0, PAGE_SIZE * 10);

    printf("=== Test Start ===\n");

    // ==========================================
    // 測試案例 1: Small Access (Miss -> Classify)
    // ==========================================
    printf("Test 1: Small Read (Expect: Miss -> Classify Area)\n");
    char *page1 = memory;
    // 讀取 Page 1 的第 0 個 Chunk (0~255 bytes)
    m5_work_begin(0, 0); // 標記開始，方便看 stats
    touch_addr(page1 + 0); 
    m5_work_end(0, 0);

    // ==========================================
    // 測試案例 2: Isolated Hotspot (Classify -> Host)
    // ==========================================
    // 假設你的 thresholdIsolated 是 8
    printf("Test 2: Isolated Hotspot (Expect: Hit Classify x8 -> Migrate Host)\n");
    char *page2 = memory + PAGE_SIZE; // 換一個新的 Page
    
    // 先讀一次，讓它進 Classify Queue
    touch_addr(page2 + 0); 

    // 連續讀取同一個 Chunk 10 次
    for (int i = 0; i < 10; i++) {
        touch_addr(page2 + 0); 
    }

    // ==========================================
    // 測試案例 3: Distributed Hotspot (Classify -> Host)
    // ==========================================
    // 假設你的 thresholdDistributed 是 4
    printf("Test 3: Distributed Hotspot (Expect: Access diff chunks -> Migrate Host)\n");
    char *page3 = memory + (PAGE_SIZE * 2); // 再換一個新 Page

    // 依序讀取 Page 3 裡面的 Chunk 0, 1, 2, 3, 4, 5
    // 這樣 bitmap 的 count 就會增加
    for (int i = 0; i < 6; i++) {
        touch_addr(page3 + (i * CHUNK_SIZE));
    }

    // ==========================================
    // 測試案例 4: Large Access (Direct Host)
    // ==========================================
    printf("Test 4: Large Access (Expect: Direct Host Cache)\n");
    char *page4 = memory + (PAGE_SIZE * 3);
    
    // 這裡我們沒辦法直接發出一個 4KB 的指令給 CPU (除非用 DMA)
    // 但我們可以透過迴圈快速讀取，看 gem5 是否會合併，
    // 或者我們依賴你程式碼中 "Cross Pages/Chunks Access" 的邏輯
    // 測試跨 Page 存取：
    volatile char val = *(page4 + PAGE_SIZE - 1); // Page4 最後一個 byte
    volatile char val2 = *(page4 + PAGE_SIZE);    // Page5 第一個 byte
    // 這樣未必能觸發你的 handleLargeAccess，取決於 CPU 發出的 Packet Size
    // 你的 handleLargeAccess 主要是看 pkt->getSize() > 256
    // 在 SE Mode 一般 CPU request 都是 64 Bytes (Cache Line)。
    // *重要*：這部分可能需要修改 gem5 Cache 設定讓 Request 變大，或是只測小存取。

    printf("=== Test End ===\n");
    free(memory);
    return 0;
}