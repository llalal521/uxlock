#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib
export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH

delay=0
core=6
time=1
LONG_CRI=8
SHORT_CRI=8
uxthread=6

echo "tas "
for i in 6
do
        $LITL_DIR/libspinlock_spinlock.sh  $LITL_DIR/bin/bench -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result
        $LOCAL_DIR/measure.sh ./result $i $uxthread 0
        sleep 1
done

echo "mcs "
for i in 6
do
        $LITL_DIR/libmcs_spinlock.sh  $LITL_DIR/bin/bench -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result
        $LOCAL_DIR/measure.sh ./result $i $uxthread 0
        sleep 1
done

echo "uta "
for i in 6
do
        $LITL_DIR/libuta_original.sh  $LITL_DIR/bin/uta_bench -u $uxthread -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result
        $LOCAL_DIR/measure.sh ./result $i $uxthread 0
done

echo "utafts "
for i in  6
do
        $LITL_DIR/libutafts_original.sh  $LITL_DIR/bin/utafts_bench -u $uxthread -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result
        $LOCAL_DIR/measure.sh ./result $i $uxthread 0
        sleep 1
done

echo "utascl "
for i in  6
do
        $LITL_DIR/libutascl_original.sh  $LITL_DIR/bin/utascl_bench -u $uxthread -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result
        $LOCAL_DIR/measure.sh ./result $i $uxthread 0
        sleep 1
done

rm result

