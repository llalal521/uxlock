#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib
export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH

delay=0
core=10
time=1
LONG_CRI=8
SHORT_CRI=8
uxthread=5

# echo "pthread_lock "
# for i in 20
# do
#         $LITL_DIR/bin/bench_block -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result
#         # $LOCAL_DIR/measure.sh ./result  $i $i 0
#         sleep 1
# done

# bash ../libutablocking_spin_then_park.sh ../bin/uta_bench_block -u 10 -t 20 -T 1 -S 10 -d 0 -s 8 -g 8 > result
# $LOCAL_DIR/measure.sh ./result 20 20 0
# echo "tas "
# for i in 20
# do
#         $LITL_DIR/libspinlock_spin_then_park.sh $LITL_DIR/bin/bench_block -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result
#         $LOCAL_DIR/measure.sh ./result $i $i 0
#         sleep 1
# done

echo "utablocking "
for i in 10
do
        $LITL_DIR/libutablocking_spin_then_park.sh  $LITL_DIR/bin/uta_bench_block -u $uxthread -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI 
done
# rm result

