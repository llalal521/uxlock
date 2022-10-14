#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib
export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH

delay=1000
cs=64
time=2
uxthread=20

echo -n "mutex  "
$LITL_DIR/libpthreadinterpose_original.sh $LITL_DIR/bin/bench_block -t 20 -T $time -d $delay -s $cs > result
$LOCAL_DIR/measure.sh ./result 20 20 0
sleep $time

echo -n "MCS    "
$LITL_DIR/libmcs_spin_then_park.sh $LITL_DIR/bin/bench_block -t 20 -T $time -d $delay -s $cs > result
$LOCAL_DIR/measure.sh ./result 20 20 0
sleep $time

echo -n "proto  "
$LITL_DIR/libutablocking_original.sh $LITL_DIR/bin/bench_block -t 20 -T $time -d $delay -s $cs > result
$LOCAL_DIR/measure.sh ./result 20 20 0
sleep $time

