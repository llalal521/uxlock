#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib
export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH

delay=100
cs=8
time=5
thread=20

echo "mutex  "
$LITL_DIR/libpthreadinterpose_original.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs > result
$LOCAL_DIR/measure.sh ./result $thread $thread 0
sleep $time

echo "MCS    "
$LITL_DIR/libmcs_spin_then_park.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs > result
$LOCAL_DIR/measure.sh ./result $thread $thread 0
sleep 1

echo "proto  "
$LITL_DIR/libutablocking_original.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs >  result
$LOCAL_DIR/measure.sh ./result $thread $thread 0
sleep 1

echo "Mute  "
$LITL_DIR/libmutexee_original.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs >  result
$LOCAL_DIR/measure.sh ./result $thread $thread 0
sleep 1