import matplotlib
matplotlib.use('Agg')
import matplotlib.ticker as ticker
import numpy as np
import os

# --- Configuration & Metrics ---
METRICS = {
    # 次數 (Counts)
    "hit_host":           "system.cxl_ssd.readHitsHost",
    "hit_classify":       "system.cxl_ssd.readHitsClassify",
    "hit_store":          "system.cxl_ssd.readHitsStore",
    "hit_dirty":          "system.cxl_ssd.readHitsDirty",
    "large_access":       "system.cxl_ssd.largeAccesses",
    "small_access":       "system.cxl_ssd.smallAccesses",
    "migration_cnode":    "system.cxl_ssd.migrationFromCNode",
    "migration_snode":    "system.cxl_ssd.migrationFromSNode",

    # 延遲時間 (Total Latency Ticks -> converted to us in parse_stats)
    "time_mig_cnode":     "system.cxl_ssd.migrationFromCNodeLatency", 
    "time_mig_snode":     "system.cxl_ssd.migrationFromSNodeLatency",
    "time_host":          "system.cxl_ssd.hitsHostLatency",
    "time_classify":      "system.cxl_ssd.hitsClassifyLatency",
    "time_store":         "system.cxl_ssd.hitsStoreLatency",
    "time_dirty":         "system.cxl_ssd.hitsDirtyLatency",
    "time_large":         "system.cxl_ssd.largeAccessLatency",
    "time_small":         "system.cxl_ssd.smallAccessLatency"
}

# Color Scheme
COLORS = {
    'Host Hit':           '#2ca02c',  # Green
    'Dirty Hit':          '#d62728',  # Red
    'Classify Hit':       '#1f77b4',  # Blue
    'Store Hit':          '#ff7f0e',  # Orange
    'Large Access':       '#9467bd',  # Purple
    'Small Access':       '#8c564b',  # Brown
    'Migration (C-Node)': '#7f7f7f',  # Dark Gray
    'Migration (S-Node)': '#c7c7c7',  # Light Gray
}

def parse_stats(folder_path):
    stats_file = os.path.join(folder_path, "stats.txt")
    data = {k: 0.0 for k in METRICS.keys()}

    if not os.path.exists(stats_file):
        print(f"⚠️  找不到檔案: {stats_file}")
        return data

    try:
        with open(stats_file, "r") as f:
            for line in f:
                parts = line.split()
                if len(parts) < 2: continue
                name = parts[0]
                for key, metric_name in METRICS.items():
                    if name == metric_name:
                        try:
                            val = float(parts[1])
                            # --- 單位轉換 (Ticks/ps -> us) ---
                            # 1 us = 1,000,000 ps
                            if key.startswith("time_"):
                                val = val / 1_000_000.0
                            data[key] = val
                        except ValueError:
                            pass
        return data
    except Exception as e:
        print(f"❌ 讀取錯誤 {stats_file}: {e}")
        return data

def load_all_results(experiments):
    return {label: parse_stats(folder) for label, folder in experiments}

# ==========================================
# 📊 Detailed Statistics Summary Function
# ==========================================
def print_statistics_summary(results, labels, version, output_file, save_to_file=False):
    """Print detailed breakdown of hits, misses, and latency (Updated for us units & split migration)"""
    
    output_lines = []
    output_lines.append(f"\n{'='*70}")
    output_lines.append(f"📊 STATISTICS SUMMARY - VERSION {version}")
    output_lines.append(f"{'='*70}\n")
    
    for label in labels:
        r = results[label]
        
        # --- 1. Request Counts ---
        r_host = r.get("hit_host", 0)
        r_classify = r.get("hit_classify", 0)
        r_store = r.get("hit_store", 0)
        r_dirty = r.get("hit_dirty", 0)
        
        # Flash accesses (Misses)
        r_large = r.get("large_access", 0)
        r_small = r.get("small_access", 0)
        r_flash_total = r_large + r_small
        
        # Migrations (Split C-Node / S-Node)
        r_mig_c = r.get("migration_cnode", 0)
        r_mig_s = r.get("migration_snode", 0)
        r_mig_total = r_mig_c + r_mig_s
        
        # Total Requests (excluding migrations as they are internal overhead)
        total_req = r_host + r_classify + r_store + r_dirty + r_flash_total
        if total_req == 0: total_req = 1
        
        # --- 2. Time Breakdown (Assumes values are already converted to us) ---
        t_host = r.get("time_host", 0.0)
        t_classify = r.get("time_classify", 0.0)
        t_store = r.get("time_store", 0.0)
        t_dirty = r.get("time_dirty", 0.0)
        
        # Migration Time
        t_mig_c = r.get("time_mig_cnode", 0.0)
        t_mig_s = r.get("time_mig_snode", 0.0)
        t_mig_total = t_mig_c + t_mig_s
        
        # Flash Time
        t_large = r.get("time_large", 0.0)
        t_small = r.get("time_small", 0.0)
        t_flash_total = t_large + t_small
        
        total_time = t_host + t_classify + t_store + t_dirty + t_mig_total + t_flash_total
        if total_time == 0: total_time = 1
        
        # --- 3. Output Formatting ---
        output_lines.append(f"{'─'*70}")
        output_lines.append(f"🔹 {label}")
        output_lines.append(f"{'─'*70}")
        
        output_lines.append(f"\n📍 Request Distribution:")
        output_lines.append(f"   Host Hits:     {int(r_host):>12,}  ({r_host/total_req*100:>6.2f}%)")
        output_lines.append(f"   Classify Hits: {int(r_classify):>12,}  ({r_classify/total_req*100:>6.2f}%)")
        output_lines.append(f"   Store Hits:    {int(r_store):>12,}  ({r_store/total_req*100:>6.2f}%)")
        output_lines.append(f"   Dirty Hits:    {int(r_dirty):>12,}  ({r_dirty/total_req*100:>6.2f}%)")
        output_lines.append(f"   Flash Access:  {int(r_flash_total):>12,}  ({r_flash_total/total_req*100:>6.2f}%)")
        output_lines.append(f"     └ Small:     {int(r_small):>12,}")
        output_lines.append(f"     └ Large:     {int(r_large):>12,}")
        output_lines.append(f"   {'─'*50}")
        output_lines.append(f"   Total Req:     {int(total_req):>12,}  (100.00%)")
        output_lines.append(f"\n   ⚙️ Internal Overhead (Migrations):")
        output_lines.append(f"   Mig (C-Node):  {int(r_mig_c):>12,}")
        output_lines.append(f"   Mig (S-Node):  {int(r_mig_s):>12,}")
        output_lines.append(f"   Total Mig:     {int(r_mig_total):>12,}")
        
        output_lines.append(f"\n⏱️  Time Breakdown (us):")
        output_lines.append(f"   Host Time:     {t_host:>12,.2f}  ({t_host/total_time*100:>6.2f}%)")
        output_lines.append(f"   Classify Time: {t_classify:>12,.2f}  ({t_classify/total_time*100:>6.2f}%)")
        output_lines.append(f"   Store Time:    {t_store:>12,.2f}  ({t_store/total_time*100:>6.2f}%)")
        output_lines.append(f"   Dirty Time:    {t_dirty:>12,.2f}  ({t_dirty/total_time*100:>6.2f}%)")
        output_lines.append(f"   Migration Time:{t_mig_total:>12,.2f}  ({t_mig_total/total_time*100:>6.2f}%)")
        output_lines.append(f"     └ C-Node:    {t_mig_c:>12,.2f}")
        output_lines.append(f"     └ S-Node:    {t_mig_s:>12,.2f}")
        output_lines.append(f"   Flash Time:    {t_flash_total:>12,.2f}  ({t_flash_total/total_time*100:>6.2f}%)")
        output_lines.append(f"   {'─'*50}")
        output_lines.append(f"   Total Time:    {total_time:>12,.2f}  (100.00%)")
        output_lines.append("")
    
    # Print to console
    output_text = "\n".join(output_lines)
    # print(output_text)
    
    # Save to file if requested
    if save_to_file:
        with open(output_file, 'w') as f:
            f.write(output_text)
        print(f"✅ Statistics saved to: {output_file}")
    
    return output_text

# ==========================================
# 📈 Plotting Function
# ==========================================
def plot_stacked_bar(ax, labels, component_data, title, ylabel, use_log=False, force_sci_y=False):
    """
    通用堆疊長條圖繪製函數
    :param force_sci_y: 強制 Y 軸使用科學記號顯示 (例如 1.5 x 10^3)
    """
    x_pos = np.arange(len(labels))
    bottom_vals = np.zeros(len(labels))
    
    for category, values in component_data.items():
        color = COLORS.get(category, 'grey')
        ax.bar(x_pos, values, bottom=bottom_vals, label=category, color=color, edgecolor='white', width=0.6)
        bottom_vals += np.array(values)

    ax.set_title(title, fontweight='bold', fontsize=12)
    ax.set_ylabel(ylabel, fontsize=10)
    ax.grid(axis='y', linestyle='--', alpha=0.3, which='both')
    
    if use_log:
        ax.set_yscale('log')
    elif force_sci_y:
        # 強制使用科學記號
        formatter = ticker.ScalarFormatter(useMathText=True)
        formatter.set_powerlimits((0, 0)) # 強制顯示 10^x
        ax.yaxis.set_major_formatter(formatter)
    else:
        # 一般數值加逗號
        ax.yaxis.set_major_formatter(ticker.StrMethodFormatter('{x:,.0f}'))
    
    ax.set_xticks(x_pos)
    ax.set_xticklabels(labels, rotation=15, ha='right')