import matplotlib.pyplot as plt
import numpy as np
import os
import sys

# ==========================================
# 🔧 設定區域
# ==========================================
version = "v3"
EXPERIMENTS = [
    ("Linear (Bi-Tiered)", f"m5out_{version}_linear_256"),
    ("Random (Bi-Tiered)", f"m5out_{version}_random_256"),
    ("Linear (Base)", f"m5out_{version}_linear_baseline"),
    ("Random (Base)", f"m5out_{version}_random_baseline")
]

# 1. 新增 Latency 相關的統計數據映射
METRICS = {
    # --- 次數 (Counts) ---
    "hit_host":     "system.cxl_ssd.readHitsHost",
    "hit_classify": "system.cxl_ssd.readHitsClassify",
    "hit_store":    "system.cxl_ssd.readHitsStore",
    "hit_dirty":    "system.cxl_ssd.readHitsDirty",
    "miss_flash":   "system.cxl_ssd.readMisses",
    "migrations":   "system.cxl_ssd.migrations",
    
    # --- 延遲分佈 (Distribution) ---
    "lat_fast":     "system.cxl_ssd.latencyDistribution::0-4.1943e+06",
    "lat_slow":     "system.cxl_ssd.latencyDistribution::9.6469e+07-1.00663e+08",

    # --- 新增: 總延遲時間組成 (Total Latency Ticks) ---
    "time_total":    "system.cxl_ssd.totalLatency",
    "time_mig":      "system.cxl_ssd.migrationLatency",
    "time_host":     "system.cxl_ssd.hitsHostLatency",
    "time_classify": "system.cxl_ssd.hitsClassifyLatency",
    "time_store":    "system.cxl_ssd.hitsStoreLatency",
    "time_dirty":    "system.cxl_ssd.hitsDirtyLatency",
    "time_large":    "system.cxl_ssd.largeAccessLatency",
    "time_small":    "system.cxl_ssd.smallAccessLatency" # 對應 Flash Access
}

def parse_stats(folder_path):
    stats_file = os.path.join(folder_path, "stats.txt")
    data = {k: 0.0 for k in METRICS.keys()} 
    
    if not os.path.exists(stats_file):
        print(f"⚠️  找不到 {stats_file}")
        return data

    try:
        with open(stats_file, "r") as f:
            for line in f:
                parts = line.split()
                if not parts: continue
                name = parts[0]
                for key, metric_name in METRICS.items():
                    if name == metric_name:
                        try:
                            data[key] = float(parts[1])
                        except:
                            pass
        return data
    except Exception as e:
        print(f"❌ 讀取錯誤: {e}")
        return data

def main():
    results = {label: parse_stats(folder) for label, folder in EXPERIMENTS}
    labels = list(results.keys())

    # 改成 2x2 的網格佈局，這樣放入 4 張圖比較好看
    fig, axs = plt.subplots(2, 2, figsize=(16, 12))
    (ax1, ax2), (ax3, ax4) = axs

    # ==========================================
    # 圖 1: Access Breakdown (次數堆疊)
    # ==========================================
    hit_host = np.array([results[l]["hit_host"] for l in labels])
    hit_internal = np.array([results[l]["hit_classify"] + results[l]["hit_store"] + results[l]["hit_dirty"] for l in labels])
    miss_flash = np.array([results[l]["miss_flash"] for l in labels])
    
    ax1.bar(labels, hit_host, label='Host Hit (DRAM)', color='#2ca02c', alpha=0.8, width=0.5)
    ax1.bar(labels, hit_internal, bottom=hit_host, label='Internal Hit', color='#ff7f0e', alpha=0.8, width=0.5)
    ax1.bar(labels, miss_flash, bottom=hit_host+hit_internal, label='Flash Miss', color='#d62728', alpha=0.8, width=0.5)
    
    ax1.set_title("Request Count Breakdown\n(Where are requests served?)", fontweight='bold')
    ax1.set_ylabel("Count")
    ax1.set_yscale('log') # 使用 Log Scale 因為數量可能差異很大
    ax1.legend()
    ax1.grid(axis='y', linestyle='--', alpha=0.3)
    
    # 標註 Flash Miss 次數
    for i, v in enumerate(miss_flash):
        total_h = hit_host[i] + hit_internal[i] + v
        if v > 0:
            ax1.text(i, total_h, f"Miss:\n{int(v):,}", ha='center', va='bottom', fontsize=9, fontweight='bold')

    # ==========================================
    # 圖 2: Migration Activity (遷移次數)
    # ==========================================
    migrations = [results[l]["migrations"] for l in labels]
    bars2 = ax2.bar(labels, migrations, color=['#1f77b4', '#9467bd'], width=0.5, alpha=0.9)
    
    ax2.set_title("Migration Events\n(Pages moved SSD -> DRAM)", fontweight='bold')
    ax2.set_ylabel("Count")
    ax2.set_yscale('log') # 使用 Log Scale 因為數量可能差異很大
    ax2.grid(axis='y', linestyle='--', alpha=0.3)
    
    for bar in bars2:
        h = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width()/2., h, f'{int(h):,}', ha='center', va='bottom', fontsize=10, fontweight='bold')

    # ==========================================
    # 圖 3: Latency Distribution (雙峰分佈)
    # ==========================================
    x = np.arange(len(labels))
    width = 0.35
    val_fast = [results[l]["lat_fast"] for l in labels]
    val_slow = [results[l]["lat_slow"] for l in labels]
    
    ax3.bar(x - width/2, val_fast, width, label='Fast (<4µs)', color='#2ca02c', alpha=0.8)
    ax3.bar(x + width/2, val_slow, width, label='Slow (~100µs)', color='#d62728', alpha=0.8)
    
    ax3.set_title("Latency Count Distribution\n(Fast Hits vs Slow Misses)", fontweight='bold')
    ax3.set_xticks(x)
    ax3.set_xticklabels(labels)
    ax3.set_ylabel("Count")
    ax3.set_yscale('log')
    ax3.legend()
    ax3.grid(axis='y', linestyle='--', alpha=0.3)

    # ==========================================
    # 圖 4: [新增] Total Latency Breakdown (時間組成)
    # ==========================================
    # 準備數據 (單位: Ticks)
    t_host = np.array([results[l]["time_host"] for l in labels])
    t_internal = np.array([results[l]["time_classify"] + results[l]["time_store"] + results[l]["time_dirty"] for l in labels])
    t_mig = np.array([results[l]["time_mig"] for l in labels])
    t_flash = np.array([results[l]["time_small"]+results[l]["time_large"] for l in labels])
    
    # 畫堆疊圖
    # 層順序: Host -> Internal -> Migration -> Flash
    p1 = ax4.bar(labels, t_host, label='Host Hit Time', color='#2ca02c', alpha=0.8, width=0.5)
    p2 = ax4.bar(labels, t_internal, bottom=t_host, label='Internal Hit Time', color='#ff7f0e', alpha=0.8, width=0.5)
    p3 = ax4.bar(labels, t_mig, bottom=t_host+t_internal, label='Migration Overhead', color='#9467bd', alpha=0.8, width=0.5)
    p4 = ax4.bar(labels, t_flash, bottom=t_host+t_internal+t_mig, label='Flash Access Time', color='#d62728', alpha=0.8, width=0.5)

    ax4.set_title("Total System Time Breakdown\n(Where is time spent?)", fontweight='bold')
    ax4.set_ylabel("Total Latency (Ticks)")
    ax4.set_yscale('log')
    # 因為 Flash Access 時間通常遠大於其他，這裡通常不需要 Log Scale，直接看比例
    # 如果 Host 時間太少看不見，這本身就是一個重要的結論 (Flash dominates)
    ax4.legend(loc='upper left', fontsize='small') 
    ax4.grid(axis='y', linestyle='--', alpha=0.3)

    # 在 Migration 區塊旁標註佔比 (如果看的見的話)
    # 這裡我們計算 Migration 佔總時間的百分比並印在 Console
    print("\n📊 Time Breakdown Analysis:")
    for i, l in enumerate(labels):
        total = t_host[i] + t_internal[i] + t_mig[i] + t_flash[i]
        if total > 0:
            print(f"host={t_host[i]/total * 100:.2f}%, internal={t_internal[i]/total * 100:.4f}%, mig={t_mig[i]/total * 100:.4f}%, flash={t_flash[i]/total * 100:.2f}%")
            print(f"host={t_host[i]}, internal={t_internal[i]}, mig={t_mig[i]}, flash={t_flash[i]}")
            mig_pct = (t_mig[i] / total) * 100
            flash_pct = (t_flash[i] / total) * 100
            print(f"  [{l}] Migration Time: {mig_pct:.4f}%, Flash Time: {flash_pct:.2f}%")
            # 在圖上標註 Migration 數值 (如果夠大)
            if t_mig[i] > 0:
                 # 標在 Migration bar 的中間
                h_base = t_host[i] + t_internal[i]
                ax4.text(i, h_base + t_mig[i]/2, f"{mig_pct:.4f}%", ha='center', va='center', fontsize=8, color='black')

    output_file = f"cxl_full_analysis_{version}.png"
    plt.savefig(output_file)
    print(f"\n✅ 完整分析圖表已儲存: {output_file}")
    plt.show()

if __name__ == "__main__":
    main()