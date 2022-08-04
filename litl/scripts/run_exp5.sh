#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib

CORENUM=16
DELAYTIME=0
LONG_CRI=32
SHORT_CRI=32
time=10
export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH

echo -n "UTA "
$LITL_DIR/libuta_original.sh $LITL_DIR/bin/uta_bench -t 10 -T 1 -u 10 -S 6 -s 2 -g 1024 -d $DELAYTIME > result
$LOCAL_DIR/measure.sh ./result 10 10

sleep 1
echo -n "UPPER "
$LITL_DIR/libuxactive_original.sh $LITL_DIR/bin/uxactive_bench -t 10 -T 1 -u 6 -S 6 -s 2 -g 1024 -d $DELAYTIME  > result
$LOCAL_DIR/measure.sh ./result 10 10

rm result
