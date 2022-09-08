#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib
export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH

delay=0
core=5
time=1
LONG_CRI=128
SHORT_CRI=8
uxthread=10

echo "pthread_lock "
for i in 20
do
        $LITL_DIR/bin/bench_block -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $SHORT_CRI > result
        $LOCAL_DIR/measure.sh ./result  $i 5 0
        sleep 1
done

# echo "tas "
# for i in `seq 10 20`
# do
#         $LITL_DIR/libspinlock_spin_then_park.sh $LITL_DIR/bin/bench_block -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result
#         $LOCAL_DIR/measure.sh ./result $i $i 0
#         sleep 1
# done

# echo "tas "
# for i in 20
# do
#         $LITL_DIR/libspinlock_spin_then_park.sh $LITL_DIR/bin/bench_block -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result
#         $LOCAL_DIR/measure.sh ./result 20 5 0
#         sleep 1
# done

# echo "utablocking "
# for i in 20
# do
#         $LITL_DIR/libutablocking_spin_then_park.sh  $LITL_DIR/bin/uta_bench_block -u $uxthread -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result 
#         $LOCAL_DIR/measure.sh ./result 20 20 0
# done

# rm result

