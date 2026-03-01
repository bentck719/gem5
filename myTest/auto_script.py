#!/usr/bin/env python3
import subprocess
import resource
import sys
import time
from pathlib import Path
from rich.console import Console
from rich.progress import (
    Progress,
    SpinnerColumn,
    BarColumn,
    TextColumn,
    TimeElapsedColumn,
    TimeRemainingColumn,
)
from rich.panel import Panel
from rich.table import Table
from rich import print as rprint

# --- 定義記憶體限制函數 ---
def set_mem_limit():
    mem_limit = 24 * 1024 * 1024 * 1024 
    resource.setrlimit(resource.RLIMIT_AS, (mem_limit, mem_limit))

# --- 初始化 Rich Console ---
console = Console()

# --- 設定區 ---
GEM5_BIN = "../build/X86/gem5.opt"  # gem5 執行檔路徑
SCRIPT = "run_cxl_test.py"          # 你的模擬腳本

version = "v2"

# 定義要跑的實驗參數
# Stride-based NVM classification: stride values determine access patterns
# Common stride values: 64 (cache-line), 128 (cache-line variants), 4096 (page-aligned)
TESTS = [
    # YCSB Workload Examples (deprecated - kept for reference)
    # {"lat": "0", "workload_mode": "RANDOM", "kind": "ycsb", "workload_type": "B", "host_dram_size": "8GiB"},   
    # {"lat": "256", "workload_mode": "RANDOM", "kind": "ycsb", "workload_type": "B", "host_dram_size": "6GiB"},
    # {"lat": "0", "workload_mode": "LINEAR", "kind": "ycsb", "workload_type": "B", "host_dram_size": "8GiB"},
    # {"lat": "256", "workload_mode": "LINEAR", "kind": "ycsb", "workload_type": "B", "host_dram_size": "6GiB"}

    # Stride-based NVM workloads: stride replaces Random/Linear classification
    # {"lat": "0", "kind": "nvm", "host_dram_size": "8GiB", "stride": 256},
    # {"lat": "0", "kind": "nvm", "host_dram_size": "8GiB", "stride": 1024},
    # {"lat": "0", "kind": "nvm", "host_dram_size": "8GiB", "stride": 4096+128},
    # {"lat": "0", "kind": "nvm", "host_dram_size": "8GiB", "stride": 4096+1024},

    {"lat": "256", "kind": "nvm", "host_dram_size": "6GiB", "stride": 64, "threshold_distributed": 2},
    {"lat": "256", "kind": "nvm", "host_dram_size": "6GiB", "stride": 64, "threshold_distributed": 8},
    {"lat": "256", "kind": "nvm", "host_dram_size": "6GiB", "stride": 64, "threshold_distributed": 12},

    {"lat": "256", "kind": "nvm", "host_dram_size": "6GiB", "stride": 256, "threshold_distributed": 2},
    {"lat": "256", "kind": "nvm", "host_dram_size": "6GiB", "stride": 256, "threshold_distributed": 8},
    {"lat": "256", "kind": "nvm", "host_dram_size": "6GiB", "stride": 256, "threshold_distributed": 12},
    
    {"lat": "256", "kind": "nvm", "host_dram_size": "6GiB", "stride": 1024, "threshold_distributed": 2},
    {"lat": "256", "kind": "nvm", "host_dram_size": "6GiB", "stride": 1024, "threshold_distributed": 8},
    {"lat": "256", "kind": "nvm", "host_dram_size": "6GiB", "stride": 1024, "threshold_distributed": 12},

    {"lat": "256", "kind": "nvm", "host_dram_size": "6GiB", "stride": 4096+128, "threshold_distributed": 2},
    {"lat": "256", "kind": "nvm", "host_dram_size": "6GiB", "stride": 4096+128, "threshold_distributed": 8},
    {"lat": "256", "kind": "nvm", "host_dram_size": "6GiB", "stride": 4096+128, "threshold_distributed": 12},


    {"lat": "256", "kind": "nvm", "host_dram_size": "6GiB", "stride": 4096+1024, "threshold_distributed": 2},
    {"lat": "256", "kind": "nvm", "host_dram_size": "6GiB", "stride": 4096+1024, "threshold_distributed": 8},
    {"lat": "256", "kind": "nvm", "host_dram_size": "6GiB", "stride": 4096+1024, "threshold_distributed": 12}
]

# --- 歡迎畫面 ---
welcome_text = """
[bold cyan]🚀 自動化測試腳本啟動[/bold cyan]

[yellow]正在批量執行 gem5 CXL 模擬...[/yellow]
[italic grey70]快去買早餐吧，這裡交給我！🥐[/italic grey70]
"""
console.print(Panel(welcome_text, title="gem5 Batch Runner", border_style="green"))

# --- 檢查執行檔 ---
if not Path(GEM5_BIN).exists():
    console.print(f"[bold red]❌ 找不到 gem5 執行檔：{GEM5_BIN}[/bold red]")
    console.print("   請確認路徑是否正確 (例如是否需要退兩層 ../../)")
    sys.exit(1)

# 用來儲存最後結果的列表
results_summary = []

# --- 開始執行 ---
# 定義進度條的樣式
progress_columns = [
    SpinnerColumn(),              # 轉圈圈動畫
    TextColumn("[bold blue]{task.description}"), # 任務描述
    BarColumn(),                  # 進度條本體
    TextColumn("[progress.percentage]{task.percentage:>3.0f}%"), # 百分比
    TimeElapsedColumn(),          # 已過時間
    TimeRemainingColumn()         # 剩餘時間
]

with Progress(*progress_columns, console=console, expand=True) as progress:
    task_id = progress.add_task("[cyan]準備開始...", total=len(TESTS))
    
    for i, test in enumerate(TESTS, 1):
        # Generate descriptive test names based on stride-based classification
        if test['kind'] == 'ycsb':
            # YCSB naming: m5out_<version>_ycsb_<workload>_<mode>_<lat>
            test_name = f"m5out_{version}_{test['kind']}_w{test.get('workload_type', 'A')}_{test.get('workload_mode', 'RANDOM').lower()}_{test['lat']}"
        else:
            # NVM stride-based naming: m5out_<version>_nvm_stride<stride>_<lat>_<threshold_distributed>
            test_name = f"m5out_{version}_{test['kind']}_stride{test['stride']}_{test['lat']}_{test['threshold_distributed']}"

        lat_val = test['lat']
        
        # 更新進度條文字
        progress.update(task_id, description=f"[cyan]正在跑 ({i}/{len(TESTS)}): {test_name} (Lat: {lat_val})")
        
        # 組合指令 - updated for new parameter structure
        cmd = [
            GEM5_BIN,
            f"--outdir={test_name}",
            SCRIPT,
            f"--lat={test['lat']}",
            f"--workload_mode={test.get('workload_mode', 'RANDOM')}",
            f"--stride={test.get('stride', 64)}",
            f"--workload={test.get('workload_type', 'A')}",
            f"--kind={test['kind']}",
            f"--threshold_distributed={test.get('threshold_distributed', 4)}",
            f"--host_dram_size={test['host_dram_size']}",
            f"--save-dir=./{test_name}"
        ]

        start_time = time.time()
        
        try:
            log_dir = Path(test_name)
            log_dir.mkdir(parents=True, exist_ok=True)

            stdout_log = log_dir / "stdout.log"
            err_log = log_dir / "error.log"

            with open(stdout_log, "w") as f_out, open(err_log, "w") as f_err:
                result = subprocess.run(
                    cmd,
                    stdout=f_out, 
                    stderr=f_err,
                    preexec_fn=set_mem_limit
                )

            duration = time.time() - start_time
            
            if result.returncode == 0:
                # 成功
                progress.console.print(f"[green]✅ 完成[/green] : [bold]{test_name}[/bold] [dim]({duration:.1f}s)[/dim]")
                results_summary.append({"name": test_name, "status": "Pass", "color": "green", "time": f"{duration:.1f}s"})
            else:
                # 失敗
                progress.console.print(f"[bold red]❌ 失敗[/bold red] : {test_name} [dim](Code: {result.returncode})[/dim]")
                
                progress.console.print(f"   └── 錯誤日誌已寫入: [underline]{err_log}[/underline]")
                results_summary.append({"name": test_name, "status": "Fail", "color": "red", "time": f"{duration:.1f}s"})

        except Exception as e:
            progress.console.print(f"[bold red]💥 例外錯誤[/bold red] : {e}")
            results_summary.append({"name": test_name, "status": "Error", "color": "red", "time": "N/A"})

        # 更新進度條
        progress.advance(task_id)

# --- 顯示總結報表 ---
console.print("\n")
table = Table(title="📊 測試結果總結", show_header=True, header_style="bold magenta")
table.add_column("測試名稱", style="cyan")
table.add_column("狀態", justify="center")
table.add_column("耗時", justify="right")

for res in results_summary:
    status_str = f"[{res['color']}]{res['status']}[/{res['color']}]"
    table.add_row(res["name"], status_str, res["time"])

console.print(table)

# 結束訊息
end_panel = Panel(
    "[bold green]全部跑完啦！早餐好吃嗎？☕️[/bold green]\n請檢查上方表格確認是否有 Failed 的項目。",
    border_style="green"
)
console.print(end_panel)
