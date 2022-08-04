
for i in 'seq 4 16'
../bin/ux_bench_uta -t i -d 100 > result
./measure.sh ./result 16