#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path
from datetime import datetime

# version = datetime.now().strftime("%Y-%m-%d-%H:%M:%S")
version = "v4"

# --- 設定區 ---
GEM5_BIN = "../build/X86/gem5.opt"  # gem5 執行檔路徑
SCRIPT = "run_cxl_test.py"          # 你的模擬腳本

# 定義要跑的實驗參數 (lat 0 和 256)
TESTS = [
    {"name": f"m5out_{version}_random_baseline", "lat": "0", "mode": "RANDOM", "host_dram_size": "8GiB"},   
    {"name": f"m5out_{version}_random_256",      "lat": "256", "mode": "RANDOM", "host_dram_size": "6GiB"},
    {"name": f"m5out_{version}_linear_baseline", "lat": "0", "mode": "LINEAR", "host_dram_size": "8GiB"},
    {"name": f"m5out_{version}_linear_256",      "lat": "256", "mode": "LINEAR", "host_dram_size": "6GiB"} 
]

print("🚀 學長幫你準備好了，開始批量執行 gem5 測試...")
print("🥐 快去買早餐吧，回來就有數據了！\n")

# 確保 gem5 執行檔存在 (簡單檢查)
if not Path(GEM5_BIN).exists():
    print(f"❌ 找不到 gem5 執行檔：{GEM5_BIN}")
    print("請確認路徑是否正確 (例如是否需要退兩層 ../../)")
    sys.exit(1)

for i, test in enumerate(TESTS, 1):
    print(f"[{i}/{len(TESTS)}] 正在跑 {test['name']} ({test['lat']})...")
    
    # 組合指令
    cmd = [
        GEM5_BIN,
        f"--outdir={test['name']}",
        SCRIPT,
        f"--lat={test['lat']}",     
        f"--workload_mode={test['mode']}",
        f"--host_dram_size={test['host_dram_size']}",
        f"--save-dir=./{test['name']}"
    ]
    
    # 印出執行的指令供檢查
    print(f"   執行指令: {' '.join(cmd)}")

    try:
        result = subprocess.run(cmd, 
                              capture_output=True, 
                              text=True) 
        
        if result.returncode == 0:
            print(f"✅ {test['name']} 完成！")
        else:
            print(f"❌ {test['name']} 失敗！Return Code: {result.returncode}")
            # 把錯誤訊息寫到 log 檔，方便你回來 Debug
            err_log = Path(test['name']) / "error.log"
            # 確保資料夾存在 (gem5 通常會建，但失敗時可能沒建)
            Path(test['name']).mkdir(exist_ok=True) 
            with open(err_log, "w") as f:
                f.write(result.stderr)
            print(f"   錯誤訊息已寫入 {err_log}")
            
    except Exception as e:
        print(f"💥 發生未預期的錯誤：{e}")
    
    print("-" * 60)

print("\n🎉 全部跑完啦！早餐好吃嗎？")