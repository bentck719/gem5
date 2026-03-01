../build/X86/gem5.opt \
--outdir=./m5out_v1_small_mem \
se_cxl.py \
--cpu-type=TimingSimpleCPU \
--mem-size=10GiB \
--cmd="./replayer" \
--options="./replay_list.csv 0" \
--host_dram_size='6GiB' \
--cxl_dram_size='2GiB'