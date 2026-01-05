#!/bin/bash

../build/X86/gem5.opt run_cxl_fs.py \
  --script ycsb_test.sh \
  --checkpoint-dir my_cpt