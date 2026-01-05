import matplotlib.pyplot as plt
import os
import sys

# ==========================================
# 🔧 設定區域
# ==========================================
version = "v3"
# 確保這些資料夾名稱跟你的 gem5 輸出一致
EXPERIMENTS = [
    ("Linear (Base)", f"m5out_{version}_linear_baseline"),
    ("Linear (Bi-Tiered)", f"m5out_{version}_linear_256"),
    ("Random (Base)", f"m5out_{version}_random_baseline"),
    ("Random (Bi-Tiered)", f"m5out_{version}_random_256")
]

# 關鍵：我們改抓 MemBus 的頻寬，代表 CPU 看到的主記憶體總頻寬
METRICS_MAPPING = {
    "avg_latency": "system.cpu.avgReadLatency",
    # "total_bw":    "system.membus.bwRead::total",  # 系統總讀取頻寬
    "cxl_bw":      "system.cxl_ssd.bwRead::total" # 如果你想畫細項可以保留
}

def parse_stats(folder_path):
    stats_file = os.path.join(folder_path, "stats.txt")
    data = {}
    
    if not os.path.exists(stats_file):
        print(f"⚠️  警告: 找不到 {stats_file}")
        return None

    try:
        with open(stats_file, "r") as f:
            for line in f:
                parts = line.split()
                if not parts: continue
                
                stat_name = parts[0]
                # 簡單的數值提取
                if stat_name in METRICS_MAPPING.values():
                    try:
                        # 有些數值後面可能有單位，只取第二個欄位
                        data[stat_name] = float(parts[1])
                    except ValueError:
                        pass
        return data
    except Exception as e:
        print(f"❌ 讀取錯誤: {e}")
        return None

def main():
    labels = []
    latencies = []
    bandwidths = []

    print(f"📊 開始分析版本: {version}")

    for label, folder in EXPERIMENTS:
        stats = parse_stats(folder)
        if stats:
            # Latency: 轉成奈秒 (假設 1 tick = 1 ps) -> 除以 1000 變 ns
            # gem5 default: 1 tick = 1 picosecond
            lat_ns = stats.get(METRICS_MAPPING["avg_latency"], 0) / 1000.0
            
            # Bandwidth: 轉成 GB/s
            bw_gbs = stats.get(METRICS_MAPPING["cxl_bw"], 0) / 1e9

            labels.append(label)
            latencies.append(lat_ns)
            bandwidths.append(bw_gbs)
            
            print(f"  -> {label}: Latency={lat_ns:.2f} ns, BW={bw_gbs:.2f} GB/s")
        else:
            print(f"  -> {label}: No Data")
            labels.append(label)
            latencies.append(0)
            bandwidths.append(0)

    # --- 繪圖 ---
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))
    
    # 配色：Baseline 用灰色，Bi-Tiered 用亮色
    colors = ['#7f7f7f' if 'Base' in l else '#1f77b4' for l in labels]

    # 左圖：Latency
    bars1 = ax1.bar(labels, latencies, color=colors, alpha=0.8, width=0.6)
    ax1.set_ylabel('Avg Read Latency (ns)')
    ax1.set_title('Average System Read Latency\n(Lower is Better)')
    ax1.grid(axis='y', linestyle='--', alpha=0.3)
    
    for bar in bars1:
        h = bar.get_height()
        ax1.text(bar.get_x() + bar.get_width()/2, h, f'{int(h)}', ha='center', va='bottom', fontweight='bold')

    # 右圖：Bandwidth
    bars2 = ax2.bar(labels, bandwidths, color=colors, alpha=0.8, width=0.6)
    ax2.set_ylabel('Total System Read Bandwidth (GB/s)')
    ax2.set_title('Total Memory Bandwidth (DRAM + CXL)\n(Higher is Better)')
    ax2.grid(axis='y', linestyle='--', alpha=0.3)

    for bar in bars2:
        h = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width()/2, h, f'{h:.1f}', ha='center', va='bottom', fontweight='bold')

    plt.suptitle(f'CXL-SSD Simulation Analysis ({version})', fontsize=16)
    plt.tight_layout()
    plt.savefig(f"result_analysis_{version}.png")
    print(f"\n✅ 圖表已產生: result_analysis_{version}.png")

if __name__ == "__main__":
    main()