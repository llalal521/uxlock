#!/bin/bash

# ARG: 1 file 2 core 3 ux number

LOCAL_DIR=$(dirname $(readlink -f "$0"))
KC_ROOT_DIR=$LOCAL_DIR/..

RECORDLEN=100000000
FILE=$1
CORENUM=$2
UXNUM=$3

echo -n "" > uxtail_tot
echo -n "" > tail_cnt

cur_len=4
end_len=$[$cur_len+$RECORDLEN-1]
ux_tot_line=0
tot_line=0

for i in `seq 1 $UXNUM`
do
    sed -n "$[$cur_len - 1]p" $FILE  >> tail_cnt
    line=`sed -n "$i p" tail_cnt | awk 'BEGIN{ORS=""} END {print($4)}'`
    ux_tot_line=$[$ux_tot_line+$line]
    tot_line=$[$tot_line+$line]
    if [ "$line" -le "$RECORDLEN" ]
    then
        end_len=$[$cur_len+$line-1]
    else
        echo "Not precise"
        exit -1
    fi
    sed -n "$cur_len, $end_len p" $FILE > tail_$i

    sort tail_$i > tail_$i-sorted
    per_core_p99_line=$[$line*99/100]
    echo -n "Core $i cnt $line p99 "
    sed -n "$per_core_p99_line p" tail_$i-sorted # p99

    cat tail_$i >> uxtail_tot
    cur_len=$[$end_len+2]
done

for i in `seq $[$UXNUM+1] $CORENUM`
do
    sed -n "$[$cur_len - 1]p" $FILE  >> tail_cnt
    line=`sed -n "$i p" tail_cnt | awk 'BEGIN{ORS=""} END {print($4)}'`
    tot_line=$[$tot_line + $line]
    if [ "$line" -le "$RECORDLEN" ]
    then
        end_len=$[$cur_len+$line-1]
    else
        echo "Not precise"
        exit -1
    fi
    sed -n "$cur_len, $end_len p" $FILE > tail_$i
    cur_len=$[$end_len+2]
done


sort -g uxtail_tot > uxtail_sorted
p99_line=$[$ux_tot_line*99/100]
p50_line=$[$ux_tot_line*50/100]

sed -n "$p50_line p" uxtail_sorted | tr '\n' '\t' # p50
awk '{sum += $1} END {printf "%3.3f\t",sum/NR}' uxtail_tot | tr '\n' '\t'  # avg
sed -n "$p99_line p" uxtail_sorted | tr '\n' '\t' # p99

sed -n "2 p" $FILE

rm -f tail_*
rm uxtail_tot
rm uxtail_sorted