# --- 自動產生 TrafficGen ---
def create_ycsb_cfg(filename, workload_type, workload_mode, base_addr, size):
    if workload_type == "A":
        read_percent = 50
    elif workload_type == "B":
        read_percent = 95
    else:
        read_percent = 100

    # 定義熱點 (Hotspot): 20% 的空間承受 80% 的流量
    hot_ratio = 0.2

    hot_limit = int(size * hot_ratio)
    total_limit = size
    
    block_size = 64
    
    # 這裡的 duration 設短一點 (10us)，讓狀態切換更頻繁，混合度更好
    state_duration = "100000000"

    # 定義發送頻率
    min_period = 64
    max_period = 128

    print(f"--- Generating Config: {filename} ---")
    
    with open(filename, "w") as f:
        f.write("INIT 0\n")
        # State 0: Initial Idle
        f.write("STATE 0 1000000000 IDLE\n")
        
        # State 1: Hot Region Access (熱區 0 ~ 20%)
        f.write(f"STATE 1 {state_duration} {workload_mode} {read_percent} {base_addr} {base_addr + hot_limit} {block_size} {min_period} {max_period} 0\n")
        
        # State 2: Cold Region Access (冷區 20% ~ 100%)
        f.write(f"STATE 2 {state_duration} {workload_mode} {read_percent} {base_addr + hot_limit} {base_addr + total_limit} {block_size}  {min_period} {max_period} 0\n")
        
        # --- Transition Logic (修正後) ---
        # 初始狀態：80% 機率進熱區, 20% 進冷區
        f.write(f"TRANSITION 0 1 0.8\n")
        f.write(f"TRANSITION 0 2 0.2\n")
        
        # State 1 (熱區) 結束後：
        # 80% 留在熱區繼續讀寫, 20% 跳去冷區
        f.write(f"TRANSITION 1 1 0.8\n")
        f.write(f"TRANSITION 1 2 0.2\n")
        
        # State 2 (冷區) 結束後：
        # 80% 跳回熱區 (因為熱區流量大), 20% 留在冷區
        f.write(f"TRANSITION 2 1 0.8\n")
        f.write(f"TRANSITION 2 2 0.2\n")

if __name__ == "__main__":
    # 產生兩個不同的 workload config
    create_ycsb_cfg("ycsb_workload_A.cfg", workload_type="A", workload_mode="RANDOM", base_addr=0, size=10*1024**3)
    # create_ycsb_cfg("ycsb_workload_B.cfg", workload_type="B", workload_mode="RANDOM", base_addr=0x440000000, size=512*1024**2)