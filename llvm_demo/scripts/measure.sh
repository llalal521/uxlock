#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
KC_ROOT_DIR=$LOCAL_DIR/..

RECORDLEN=100000000
FILE=$1
CORENUM=$2

p99_line=$[$RECORDLEN*99/100]

echo -n "" > tail_cnt
cur_len=4
ux_cnt=0
end_len=$[$cur_len+$RECORDLEN-1]
for i in `seq 1 3`
do
    sed -n "$[$cur_len - 1]p" $FILE  >> tail_cnt
    line=`sed -n "$i p" tail_cnt | awk 'BEGIN{ORS=""} END {print($4)}'`
if [ "$line" -le "$RECORDLEN" ]
then
    end_len=$[$cur_len+$line-1]
fi
    sed -n "$cur_len, $end_len p" $FILE > tail_$i
    cur_len=$[$end_len+2]
done


tot_line=0

for i in  $3
do
lit_tot_line=0
echo -n "" > tail_lit

line=`sed -n "$i p" tail_cnt | awk 'BEGIN{ORS=""} END {print($4)}'`
tot_line=$[$tot_line + $line]
if [ "$line" -le "$RECORDLEN" ]
then
    sed -n "1, $line p" tail_$i > tail_lit
else
    sed -n "1, $RECORDLEN p" tail_$i > tail_lit
fi
sort -g tail_lit  >> tail_lit_tol
done
sort -g tail_lit_tol  > tail_lit_sorted
tot_line=$[$tot_line*99/100]
sed -n "$tot_line p" tail_lit_sorted | tr '\n' '\t'
sed -n "2 p" $FILE

rm -f tail_*

