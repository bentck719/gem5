import matplotlib.pyplot as plt
import os
import sys

# ==========================================
# 🔧 設定區域 (Configuration)
# ==========================================

# 格式: ("圖表上的標籤", "資料夾路徑")
EXPERIMENTS = [
    ("Linear (Base)", "m5out_linear_baseline"),
    ("Linear (Bi-Tiered)", "m5out_linear_256"),
    ("Random (Base)", "m5out_random_baseline"),
    ("Random (Bi-Tiered)", "m5out_random_256")
    
]

# 要抓取的 gem5 統計數據名稱 (Key)
METRICS_MAPPING = {
    "avg_latency": "system.cpu.avgReadLatency",
    "bandwidth":   "system.physmem.bwRead::total"
}

# ==========================================
# 📖 讀取與解析函式
# ==========================================

def parse_stats(folder_path):
    stats_file = os.path.join(folder_path, "stats.txt")
    data = {}
    
    if not os.path.exists(stats_file):
        print(f"⚠️ 警告: 找不到檔案 {stats_file}")
        return None

    print(f"📖 正在讀取: {stats_file} ...", end="")
    try:
        with open(stats_file, "r") as f:
            for line in f:
                parts = line.split()
                if not parts:
                    continue
                
                # 檢查這一行是不是我們要的數據
                stat_name = parts[0]
                if stat_name in METRICS_MAPPING.values():
                    # gem5 的 stats 格式通常是: name value # comment
                    try:
                        value = float(parts[1])
                        # 存入字典
                        data[stat_name] = value
                    except ValueError:
                        pass
        print(" 完成！")
        return data
    except Exception as e:
        print(f"\n❌ 讀取錯誤: {e}")
        return None

# ==========================================
# 🎨 繪圖主程式
# ==========================================

def main():
    labels = []
    latencies = []
    bandwidths = []

    # 1. 讀取所有資料夾的數據
    for label, folder in EXPERIMENTS:
        stats = parse_stats(folder)
        if stats:
            # 取得 Latency (Ticks)
            lat = stats.get(METRICS_MAPPING["avg_latency"], 0)
            # 取得 Bandwidth (Bytes/s) -> 轉成 GB/s
            bw = stats.get(METRICS_MAPPING["bandwidth"], 0) / 1e9 

            labels.append(label)
            latencies.append(lat)
            bandwidths.append(bw)
        else:
            print(f"跳過 {label} (無數據)")

    if not labels:
        print("❌ 沒有讀到任何有效數據，請檢查資料夾名稱是否正確。")
        sys.exit(1)

    # 2. 開始畫圖
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))
    
    # 設定顏色 (藍色系, 若只有兩組可手動指定強調色)
    colors = ['#4c72b0' if 'Base' in l else '#c44e52' for l in labels]
    # 如果實驗很多，可以用統一顏色: colors = '#4c72b0'

    # --- 左圖: Latency ---
    bars1 = ax1.bar(labels, latencies, color=colors, width=0.5, alpha=0.9)
    ax1.set_ylabel('Avg Read Latency (Ticks)')
    ax1.set_title('Average Read Latency\n(Lower is Better)')
    ax1.grid(axis='y', linestyle='--', alpha=0.5)

    # 標註數值
    for bar in bars1:
        height = bar.get_height()
        ax1.text(bar.get_x() + bar.get_width()/2., height,
                 f'{int(height):,}',
                 ha='center', va='bottom', fontsize=11, fontweight='bold')

    # --- 右圖: Bandwidth ---
    bars2 = ax2.bar(labels, bandwidths, color='#55a868', width=0.5, alpha=0.9)
    ax2.set_ylabel('Read Bandwidth (GB/s)')
    ax2.set_title('Memory Bandwidth Utilization\n(Should be similar if saturated)')
    ax2.grid(axis='y', linestyle='--', alpha=0.5)

    # 標註數值
    for bar in bars2:
        height = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width()/2., height,
                 f'{height:.2f} GB/s',
                 ha='center', va='bottom', fontsize=11, fontweight='bold')

    plt.suptitle('Gem5 Simulation Results Analysis', fontsize=16)
    plt.tight_layout()
    
    # 存檔或顯示
    output_img = "simulation_results.png"
    plt.savefig(output_img)
    print(f"\n✅ 圖表已儲存為: {output_img}")
    plt.show()

if __name__ == "__main__":
    main()