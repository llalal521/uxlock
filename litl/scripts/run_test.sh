#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib
export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH

delay=0
cs=64
time=2
thread=20

# echo "mutex  "
# for cs in 8 16 32 64 128 256 512 
# do
# $LITL_DIR/libpthreadinterpose_original.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs > result
# $LOCAL_DIR/measure.sh ./result $thread $thread 0
# sleep $time
# done
# echo "MCS    "
# $LITL_DIR/libmcs_spin_then_park.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs > result
# $LOCAL_DIR/measure.sh ./result $thread $thread 0
# sleep 1

# echo "proto  "
# $LITL_DIR/libutablocking_original.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs >  result
# $LOCAL_DIR/measure.sh ./result $thread $thread 0
# sleep 1

echo "Mutee  "
for cs in 512 
do
$LITL_DIR/libmutexee_original.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs >  result
$LOCAL_DIR/measure.sh ./result $thread $thread 0
sleep $time
done

# echo "utamutee  "
# for cs in 8 16 32 64 128 256 512
# do
# $LITL_DIR/libutamutexee_original.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs >  result
# $LOCAL_DIR/measure.sh ./result $thread $thread 0
# sleep 1
# done
