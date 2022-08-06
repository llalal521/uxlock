#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib

delay=0
core=4
time=1
nb_thread=10
ux_num=8
LONG_CRI=1024
SHORT_CRI=2

export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH
sleep 1
echo -n "UTA "
$LITL_DIR/libuta_original.sh $LITL_DIR/bin/uta_bench -t $nb_thread -T $time -u $ux_num -S $core -s $SHORT_CRI -g $LONG_CRI -d $delay > result
$LOCAL_DIR/measure.sh ./result $nb_thread $ux_num


