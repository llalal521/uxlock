#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib
export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH

delay=100
core=4
time=1
nb_thread=8
ux_num=4

for i in  10 20 50 100 200 300 400
do
        $LITL_DIR/libuta_original.sh  $LITL_DIR/bin/nest_bench_uta -t $nb_thread -T $time -M 2  -g 10 -P $i -d $delay -> result 
        $LOCAL_DIR/measure.sh ./result $ux_num $ux_num
        sleep 1
done


rm result

