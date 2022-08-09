#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib

DELAYTIME=0
LONG_CRI=1024
SHORT_CRI=8
short=2
time=1
thread=6

echo "UTA"
LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH $LITL_DIR/bin/uta_bench -t $thread -u $thread -T $time -S $short -s $SHORT_CRI -g $LONG_CRI -d $DELAYTIME > result
$LOCAL_DIR/measure.sh ./result $thread $thread
sleep 1

echo "UTAFTS "
LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH $LITL_DIR/bin/utafts_bench -t $thread -u $thread -T $time -S $short -s $SHORT_CRI -g $LONG_CRI -d $DELAYTIME > result
$LOCAL_DIR/measure.sh ./result $thread $thread
sleep 1

echo "MCS "
$LITL_DIR/libmcs_spinlock.sh $LITL_DIR//bin/bench -t $thread -u $thread -T $time -S $short -s $SHORT_CRI -g $LONG_CRI -d $DELAYTIME > result
$LOCAL_DIR/measure.sh ./result $thread $thread
sleep 1

rm result
