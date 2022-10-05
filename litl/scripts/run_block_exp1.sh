#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib
export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH

delay=10
core=40
time=2
LONG_CRI=512
SHORT_CRI=512
uxthread=40

# echo "pthread_lock "
# for i in  8 16 32 64 128 256 512 1024 2048
# do
#         $LITL_DIR/libpthreadinterpose_original.sh  $LITL_DIR/bin/bench_block -t 40 -T $time -S $core -d $i*$delay -s $i  > result
#         $LOCAL_DIR/measure.sh ./result 40 40 0
#         sleep 2
# done

# echo "mutexee "
# for i in  8 16 32 64 128 256 512 1024 2048
# do
#         $LITL_DIR/libmutexee_original.sh  $LITL_DIR/bin/bench_block -t 40 -T $time -S $core -d $i*$delay -s $i  > result
#         $LOCAL_DIR/measure.sh ./result 40 40 0
#         sleep 2
# done

# echo "TAS "
# for i in  8 16 32 64 128 256 512 1024 2048
# do
#         $LITL_DIR/libspinlock_spin_then_park.sh  $LITL_DIR/bin/bench_block -t 40 -T $time -S $core -d $i*$delay -s $i  > result
#         $LOCAL_DIR/measure.sh ./result 40 40 0
#         sleep 2
# done

# echo "MCS "
# for i in  8 16 32 64 128 256 512 1024 2048 
# do
#         $LITL_DIR/libmcs_spin_then_park.sh  $LITL_DIR/bin/bench_block -t 40 -T $time -S $core -d $i*$delay -s $i  > result
#         $LOCAL_DIR/measure.sh ./result 40 40 0
#         sleep 2
# done

# echo "MCSWAKE "
# for i in  8 
# do
#         $LITL_DIR/libmcswake_spin_then_park.sh  $LITL_DIR/bin/bench_block -t 40 -T $time -S $core -d $i*$delay -s $i  > result
#         $LOCAL_DIR/measure.sh ./result 40 40 0
#         sleep 2
# done

# echo "MCSSTEAL "
# for i in  8 
# do
#         $LITL_DIR/libmcssteal_spin_then_park.sh  $LITL_DIR/bin/bench_block -t 40 -T $time -S $core -d $i*$delay -s $i  > result
#         $LOCAL_DIR/measure.sh ./result 40 40 0
#         sleep 2
# done

# echo "MALTHUSIAN "
# for i in  8 
# do
#         $LITL_DIR/libmalthusian_spin_then_park.sh $LITL_DIR/bin/bench_block -t 40  -T $time -S $core -d $i*$delay -s $i  > result
#         $LOCAL_DIR/measure.sh ./result 40 40 0
#         sleep 2
# done


# echo "aslblock "
# for i in 8 16 32 64 128 256 512 1024 2048 
# do
#         $LITL_DIR/libaslblock_spin_then_park.sh $LITL_DIR/bin/bench_block -t 40  -T $time -S $core -d $i*$delay -s $i  > result
#         $LOCAL_DIR/measure.sh ./result 40 40 0
#         sleep 2
# done

echo "utablocking "
for i in 8 
do
        $LITL_DIR/libutablocking_spin_then_park.sh $LITL_DIR/bin/bench_block -t 40  -T $time -S $core -d $i*$delay -s $i  > result
        $LOCAL_DIR/measure.sh ./result 40 40 0
        sleep 2
done