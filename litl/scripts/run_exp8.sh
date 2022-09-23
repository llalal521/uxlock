#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib
export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH

delay=100
core=20
time=1
LONG_CRI=512
SHORT_CRI=8
uxthread=20

# echo "pthread_lock "
# for i in  40
# do
#         $LITL_DIR/libpthreadinterpose_original.sh  $LITL_DIR/bin/bench_block -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result
#         $LOCAL_DIR/measure.sh ./result  $i $i 0
#         sleep 1
# done

# echo "tas "
# for i in `seq 10 20`
# do
#         $LITL_DIR/libspinlock_spin_then_park.sh $LITL_DIR/bin/bench_block -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result
#         $LOCAL_DIR/measure.sh ./result $i $i 0
#         sleep 1
# done

# echo "cst "
# for i in 20
# do
#         $LITL_DIR/libcst_spin_then_park.sh $LITL_DIR/bin/cst_bench_block -u $i -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI 
#         # $LOCAL_DIR/measure.sh ./result $i $i 0
#         sleep 1
# done


# echo "MCSWAKE "
# for i in  40
# do
#         $LITL_DIR/libmcswake_spin_then_park.sh $LITL_DIR/bin/bench_block -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result
#         $LOCAL_DIR/measure.sh ./result $i $i 0
#         sleep 1
# done

# echo "MCSSTEALING "
# for i in  40
# do
#         $LITL_DIR/libmcssteal_spin_then_park.sh $LITL_DIR/bin/bench_block -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result
#         $LOCAL_DIR/measure.sh ./result $i $i 0
#         sleep 1
# done


# echo "MALTHUSIAN "
# for i in  20
# do
#         $LITL_DIR/libmalthusian_spinlock.sh $LITL_DIR/bin/bench_block -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result
#         # $LOCAL_DIR/measure.sh ./result $i $i 0
#         sleep 1
# done

# echo "MCS "
# for i in  10
# do
#         $LITL_DIR/libmcs_original.sh $LITL_DIR/bin/bench_block -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result
#         $LOCAL_DIR/measure.sh ./result $i $i 0
#         sleep 1
# done

# echo "uta "
# for i in 10
# do
#         $LITL_DIR/libuta_original.sh  $LITL_DIR/bin/uta_bench -u $i -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI
#         # $LOCAL_DIR/measure.sh ./result $i $i 0
# done

echo "utablocking "
for i in 40
do
        $LITL_DIR/libutablocking_spin_then_park.sh  $LITL_DIR/bin/uta_bench_block -u $i -t $i -T $time -S $core -d $delay -s $SHORT_CRI -g $LONG_CRI > result 
        $LOCAL_DIR/measure.sh ./result $i $i 0
        sleep 1
done

# rm result

