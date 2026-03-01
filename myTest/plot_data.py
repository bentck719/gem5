import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from plot_utils import *

def main():
    VERSION = "v2" 
    MODE = "ycsb"

    EXPERIMENTS = [
        ("Linear (Bi-Tiered)", f"m5out_{VERSION}_{MODE}_linear_256"),
        ("Random (Bi-Tiered)", f"m5out_{VERSION}_{MODE}_random_256"),
        ("Linear (Baseline)", f"m5out_{VERSION}_{MODE}_linear_0"),
        ("Random (Baseline)", f"m5out_{VERSION}_{MODE}_random_0")
    ]

    output_txt_file = f"{MODE}_{VERSION}_summary_stats.txt"
    output_img_file = f"{MODE}_{VERSION}_result_analysis.png"

    print(f"✅ Configuration loaded for VERSION: {VERSION}")
    
    # 1. Load Data
    results = load_all_results(EXPERIMENTS)
    labels = [e[0] for e in EXPERIMENTS]
    
    labels_wo_baseline = [lbl for lbl in labels if "Baseline" not in lbl]

    # 2. Print & Save Text Summary
    print_statistics_summary(results, labels, VERSION, output_txt_file, save_to_file=True)
    
    # 3. Prepare Data for Plotting
    access_components = {
        'Host Hit':      [results[l]['hit_host'] for l in labels],
        'Dirty Hit':     [results[l]['hit_dirty'] for l in labels],
        'Classify Hit':  [results[l]['hit_classify'] for l in labels],
        'Store Hit':     [results[l]['hit_store'] for l in labels],
        'Large Access':  [results[l]['large_access'] for l in labels],
        'Small Access':  [results[l]['small_access'] for l in labels],
    }

    latency_components = {
        'Host Hit':           [results[l]['time_host'] for l in labels],
        'Dirty Hit':          [results[l]['time_dirty'] for l in labels],
        'Classify Hit':       [results[l]['time_classify'] for l in labels],
        'Store Hit':          [results[l]['time_store'] for l in labels],
        'Migration (C-Node)': [results[l]['time_mig_cnode'] for l in labels],
        'Migration (S-Node)': [results[l]['time_mig_snode'] for l in labels],
        'Large Access':       [results[l]['time_large'] for l in labels],
        'Small Access':       [results[l]['time_small'] for l in labels],
    }

    latency_components_wo_miss = {
        'Host Hit':           [results[l]['time_host'] for l in labels],
        'Dirty Hit':          [results[l]['time_dirty'] for l in labels],
        'Classify Hit':       [results[l]['time_classify'] for l in labels],
        'Store Hit':          [results[l]['time_store'] for l in labels],
        'Migration (C-Node)': [results[l]['time_mig_cnode'] for l in labels],
        'Migration (S-Node)': [results[l]['time_mig_snode'] for l in labels]
    }

    migration_components = {
        'Migration (C-Node)': [results[l]['migration_cnode'] for l in labels if "Baseline" not in l],
        'Migration (S-Node)': [results[l]['migration_snode'] for l in labels if "Baseline" not in l]
    }

    # 4. Create Plots
    fig, axes = plt.subplots(2, 2, figsize=(20, 20))
    
    # Plot 1: Access Counts
    plot_stacked_bar(axes[0][0], labels, access_components, 
                     title="Data Access Distribution (Count)", 
                     ylabel="Number of Accesses", 
                     use_log=False)

    # Plot 2: Latency Breakdown (Log Scale)
    plot_stacked_bar(axes[0][1], labels, latency_components, 
                     title="Latency Distribution (us) - Log Scale", 
                     ylabel="Total Latency (us)", 
                     use_log=True)

    # Plot 3: Latency w/o Miss (Scientific Notation)
    plot_stacked_bar(axes[1][0], labels, latency_components_wo_miss, 
                     title="Latency Distribution (us) w/o Cache Miss", 
                     ylabel="Total Latency (us)", 
                     use_log=False,
                     force_sci_y=True)
    
    # Plot 4: Migration Time Breakdown
    plot_stacked_bar(axes[1][1], labels_wo_baseline, migration_components,
                     title="Migration Distribution (Count)", 
                     ylabel="Number of Accesses", 
                     use_log=False,
                     force_sci_y=True)
    
    # 5. Finalize Layout
    fig.suptitle(f"RANDOM/LINEAR Analysis", fontsize=16, fontweight='bold')

    handles, legend_labels = axes[0][1].get_legend_handles_labels()

    # Legend at Top Right
    fig.legend(handles, legend_labels, loc='upper right', bbox_to_anchor=(0.99, 0.50), 
               fontsize='medium', framealpha=1.0, shadow=True, borderpad=1)

    plt.tight_layout(rect=[0, 0, 0.92, 0.95])
    plt.savefig(output_img_file)
    
    print(f"\n✅ 分析完成! 圖表已儲存至: {output_img_file}")

if __name__ == "__main__":
    main()