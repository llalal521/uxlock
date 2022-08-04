all: cri_bench_uta ux_bench_uta nest_bench_uta


cri_bench_uta_main.bc:utils.bc uta.bc cri_bench_uta.bc
	llvm-link utils.bc uta.bc cri_bench_uta.bc  -o cri_bench_uta_main.bc

nest_bench_uta_main.bc:utils.bc uta.bc nest_bench_uta.bc
	llvm-link utils.bc uta.bc nest_bench_uta.bc  -o nest_bench_uta_main.bc

ux_bench_uta_main.bc:utils.bc uta.bc ux_bench_uta.bc
	llvm-link utils.bc uta.bc ux_bench_uta.bc  -o ux_bench_uta_main.bc

uta_pass/libutapass.so:uta_pass/uta_pass.cpp
	cd uta_pass && make

cri_bench_uta_perdict.bc:cri_bench_uta_main.bc uta_pass/libutapass.so
	opt -load uta_pass/libutapass.so -uta < cri_bench_uta_main.bc > cri_bench_uta_perdict.bc

ux_bench_uta_perdict.bc:ux_bench_uta_main.bc uta_pass/libutapass.so
	opt -load uta_pass/libutapass.so -uta < ux_bench_uta_main.bc > ux_bench_uta_perdict.bc

nest_bench_uta_perdict.bc:nest_bench_uta_main.bc uta_pass/libutapass.so
	opt -load uta_pass/libutapass.so -uta < nest_bench_uta_main.bc > nest_bench_uta_perdict.bc

cri_bench_uta.s:cri_bench_uta_perdict.bc
	llc cri_bench_uta_perdict.bc -o cri_bench_uta.s

ux_bench_uta.s:ux_bench_uta_perdict.bc
	llc ux_bench_uta_perdict.bc -o ux_bench_uta.s

nest_bench_uta.s:nest_bench_uta_perdict.bc
	llc nest_bench_uta_perdict.bc -o nest_bench_uta.s

uta.bc:uta.c
	clang uta.c  -pthread -emit-llvm -O0 -c -o uta.bc

utils.bc:utils.c
	clang utils.c  -pthread -emit-llvm -O3 -c -o utils.bc

cri_bench_uta.bc:bench/cri_bench.c
	clang bench/cri_bench.c  -pthread -emit-llvm -O3 -c -o cri_bench_uta.bc

ux_bench_uta.bc:bench/ux_bench.c
	clang bench/ux_bench.c  -pthread -emit-llvm -O3 -c -o ux_bench_uta.bc

nest_bench_uta.bc:bench/nest_bench.c
	clang bench/nest_bench.c  -pthread -emit-llvm -O3 -c -o nest_bench_uta.bc

cri_bench_uta:cri_bench_uta.s
	gcc cri_bench_uta.s -lpthread -lpapi -no-pie -o bin/cri_bench_uta

ux_bench_uta:ux_bench_uta.s
	gcc ux_bench_uta.s -lpthread -lpapi -no-pie -o bin/ux_bench_uta

nest_bench_uta:nest_bench_uta.s
	gcc nest_bench_uta.s -lpthread -lpapi -no-pie -o bin/nest_bench_uta
	
clean:
	rm -rf *.s *.bc result 
