#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib

CORENUM=16
DELAYTIME=0
LONG_CRI=1024
SHORT_CRI=2
core=6
time=1
nb_thread=10

export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH
sleep 1
echo -n "UTA "
$LITL_DIR/libuta_original.sh $LITL_DIR/bin/uta_bench -t $nb_thread -T $time -u 10 -S $core -s $SHORT_CRI -g $LONG_CRI -d $DELAYTIME > result
$LOCAL_DIR/measure.sh ./result $nb_thread $nb_thread

sleep 1
echo -n "UPPER "
$LITL_DIR/libuxactive_original.sh $LITL_DIR/bin/uxactive_bench -t $nb_thread -T $time -u 6 -S $core -s $SHORT_CRI -g $LONG_CRI -d $DELAYTIME  > result
$LOCAL_DIR/measure.sh ./result $nb_thread $nb_thread

rm result
