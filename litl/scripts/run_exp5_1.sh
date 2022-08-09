#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib

CORENUM=16
DELAYTIME=100
LONG_CRI=32
SHORT_CRI=32
time=10
export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH

echo -n "MCS "
$LITL_DIR/libmcs_spinlock.sh $LITL_DIR/bin/bench -t 10 -T 1 -s 12 -g 12 -d $DELAYTIME  > result0
$LOCAL_DIR/measure.sh ./result0 10 10
sleep 1

for i in `seq 0 10`
do
        echo -n "UTA $i "
        $LITL_DIR/libuta_original.sh $LITL_DIR/bin/uta_bench -t 10 -T 1 -u $i -s 12 -g 12 -d $DELAYTIME > result
        $LOCAL_DIR/measure.sh ./result 10 10
        sleep 1
done

rm result
