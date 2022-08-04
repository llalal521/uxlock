#!/bin/bash

# LITL_DIR=../
# LOCAL_DIR=./
# LITLLIB_DIR=$LITL_DIR/lib
# CORENUM=16
# DELAYTIME=100
# LONG_CRI=$1
# SHORT_CRI=$2
# SHROT_NUM=4
# MIDDLE_NUM=2
# LONG_NUM=2
# export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH

# for i in 5 10 15 20 25 30 35 40 45 50
# do
../bin/cri_bench_uta -t 8 -d 100 > result
./measure.sh ./result 3 1
./measure50.sh ./result 3 1
./measure_ux_ave.sh ./result 3 1


./measure.sh ./result 3 3
./measure50.sh ./result 3 3
./measure_ux_ave.sh ./result 3 3
# done

