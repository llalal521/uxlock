#!/bin/bash
rm -f tail_*
LOCAL_DIR=$(dirname $(readlink -f "$0"))
KC_ROOT_DIR=$LOCAL_DIR/..

RECORDLEN=100000000
FILE=$1
CORENUM=$2

uxCORENUM=$[$CORENUM/2]
nonuxCORENUM=$[$CORENUM/2]

p99_line=$[$RECORDLEN*99/100]
tot_p99_line=$[$RECORDLEN*99*$CORENUM/100]
tot_p95_line=$[$RECORDLEN*95*$CORENUM/100]
ux_p99_line=$[$RECORDLEN*99*$CORENUM/200]
ux_p95_line=$[$RECORDLEN*95*$CORENUM/200]
nonux_p99_line=$[$RECORDLEN*99*$CORENUM/200]
nonux_p95_line=$[$RECORDLEN*95*$CORENUM/200]

echo -n "" > tail_cnt
cur_len=4
ux_cnt=0
end_len=$[$cur_len+$RECORDLEN-1]
for i in `seq 1 3`
do
    sed -n "$[$cur_len - 1]p" $FILE  >> tail_cnt
    line=`sed -n "$i p" tail_cnt | awk 'BEGIN{ORS=""} END {print($4)}'`
    ux_cnt=$[$ux_cnt+$line]
    # echo $cur_len" "$line
    if [ "$line" -le "$RECORDLEN" ]
    then
        end_len=$[$cur_len+$line-1]
    fi
    sed -n "$cur_len, $end_len p" $FILE > tail_$i
    cur_len=$[$end_len+2]
done


tot_line=0

for i in $3
do
nonux_tot_line=0
echo -n "" >> tail_nonux

line=`sed -n "$i p" tail_cnt | awk 'BEGIN{ORS=""} END {print($4)}'`
tot_line=$[$tot_line + $line]
if [ "$line" -le "$RECORDLEN" ]
then
    sed -n "1, $line p" tail_$i >> tail_nonux
fi
done
sort -g tail_nonux  > tail_nonux_tol
awk '{sum += $1} END {printf "%3.3f\t",sum/NR}' tail_nonux_tol 
# sort -g tail_nonux_tol  > tail_nonux_sorted
echo $ux_cnt
rm -f tail_*

