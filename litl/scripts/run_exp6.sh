#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib
export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH

delay=0
core=4
time=1

for i in 10
do
        $LITL_DIR/libspinlock_spinlock.sh  $LITL_DIR/bin/bench -t $i -T $time -S $core -d $delay -> result
        $LOCAL_DIR/measure.sh ./result $i 10 | tr '\n' '\t'
        sleep 1

        $LITL_DIR/libmcs_spinlock.sh  $LITL_DIR/bin/bench -t $i -T $time -S $core -d $delay -> result
        $LOCAL_DIR/measure.sh ./result $i 10 | tr '\n' '\t'
        sleep 1

        $LITL_DIR/libuta_original.sh  $LITL_DIR/bin/uta_bench -u 8 -t $i -T $time -S $core -d $delay -> result
        $LOCAL_DIR/measure.sh ./result $i 6
        sleep 1
done


rm result

