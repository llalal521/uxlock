include Makefile.config

LDFLAGS=-lsspfd -lssmem -lrt -m64 -lm  -pthread
CFLAGS=-Iinclude/ -g -L/usr/local/lib -Llib
INCLUDE=-I/usr/local/include
LIB=/usr/local/lib/libpapi.a
export LD_LIBRARY_PATH=lib:$LD_LIBRARY_PATH
TARGETS=$(addprefix lib, $(ALGORITHMS))
DIR=$(addprefix obj/, $(ALGORITHMS))
BINDIR=bin
SOS=$(TARGETS:=.so)
SHS=$(TARGETS:=.sh)
export COND_VAR=0

.PRECIOUS: %.o
.SECONDARY: $(OBJS)
.PHONY: all clean format

BIN=thbench check bench cri_bench cri_bench_uta ux_cs_bench   uxactive_bench    cs_bench  \
	csupper_bench nest_bench uta_bench

BINPATH=$(addprefix $(BINDIR)/, $(BIN))


all: $(BINDIR) $(DIR) include/topology.h $(SOS) $(SHS) $(BINPATH)

no_cond_var: COND_VAR=0
no_cond_var: all


%.so: obj/
	mkdir -p lib/
	echo $@
	$(MAKE) -C src/ ../lib/$@

obj/:
	mkdir -p $@

$(DIR):
	mkdir -p $@

$(BINDIR):
	mkdir -p $@

$(SHS): src/liblock.in
	cat $< | sed -e "s/@abs_top_srcdir@/$$(echo $$(cd .; pwd) | sed -e 's/\([\/&]\)/\\\1/g')/g" > $@
	sed -i "s/@lib@/$$(basename $@ .sh).so/g" $@
	chmod a+x $@

include/topology.h: include/topology.in
	cat $< | sed -e "s/@nodes@/$$(numactl -H | head -1 | cut -f 2 -d' ')/g" > $@
	sed -i "s/@cpus@/$$(nproc)/g" $@
	sed -i "s/@cachelinesize@/128/g" $@  
	sed -i "s/@pagesize@/$$(getconf PAGESIZE)/g" $@
	sed -i 's#@cpufreq@#'$$(cat /proc/cpuinfo | grep MHz | head -1 | awk '{ x = $$4/1000; printf("%0.2g", x); }')'#g' $@
	chmod a+x $@

# micro-benchmarks
$(BINDIR)/thbench: bench/thbench.c $(DIR) $(SOS)
	gcc bench/thbench.c -pthread -O3 -Iinclude/ -g  -o $(BINDIR)/thbench

$(BINDIR)/ux_cs_bench: bench/ux_cs_bench.c $(DIR) $(SOS)
	gcc bench/ux_cs_bench.c -lpapi -pthread -O3 -Iinclude/ -g  -o $(BINDIR)/ux_cs_bench

$(BINDIR)/cri_bench: bench/cri_bench.c $(DIR) $(SOS)
	gcc bench/cri_bench.c -lpapi -pthread -O3 -Iinclude/ -g -o $(BINDIR)/cri_bench

$(BINDIR)/cri_bench_uta: bench/cri_bench.c $(DIR) $(SOS)
	gcc bench/cri_bench.c -lpapi -pthread -O3 -Iinclude/ -DLIBUTA_INTERFACE   -L./lib -luta_original -g -o $(BINDIR)/cri_bench_uta

$(BINDIR)/ux_cs_bench_uta: bench/ux_cs_bench.c $(DIR) $(SOS)
	gcc bench/ux_cs_bench.c -lpapi -pthread -O3 -Iinclude/ -DLIBUTA_INTERFACE   -L./lib -luta_original -g -o $(BINDIR)/ux_cs_bench_uta

$(BINDIR)/csupper_bench: bench/csbench.c $(DIR) $(SOS)
	gcc  bench/csbench.c -lpapi -pthread -O3 -Iinclude/ -DLIBCSUPPERBOUND_INTERFACE -L./lib -lcsupperbound_original -g  -o $(BINDIR)/csupperbound_bench

$(BINDIR)/cs_bench: bench/csbench.c $(DIR) $(SOS)
	gcc  bench/csbench.c -lpapi -pthread -O3 -Iinclude/   -L./lib -g  -o $(BINDIR)/cs_bench

$(BINDIR)/bench: bench/bench.c $(DIR) $(SOS)
	gcc  bench/bench.c -lpapi -pthread -O3 -Iinclude/ -L./lib  -g  -o $(BINDIR)/bench

$(BINDIR)/uxactive_bench: bench/bench.c $(DIR) $(SOS)
	gcc  bench/bench.c -lpapi -pthread -O3 -Iinclude/ -DLIBUXACTIVE_INTERFACE -L./lib -luxactive_original  -g  -o $(BINDIR)/uxactive_bench

$(BINDIR)/uta_bench: bench/bench.c $(DIR) $(SOS)
	gcc  bench/bench.c -lpapi -pthread -O3 -Iinclude/ -DLIBUTA_INTERFACE -L./lib  -luta_original -g  -o $(BINDIR)/uta_bench

$(BINDIR)/nest_bench: bench/nest_bench.c $(DIR) $(SOS)
	gcc  bench/nest_bench.c -lpapi -pthread -O3 -Iinclude/ -L./lib  -g  -o $(BINDIR)/nest_bench


$(BINDIR)/nest_uxcs_bench: bench/nest_bench.c $(DIR) $(SOS)
	gcc  bench/nest_bench.c -lpapi -pthread -O3 -Iinclude/ -DLIBUTA_INTERFACE -L./lib  -luta_original -g  -o $(BINDIR)/nest_uxcs_bench

$(BINDIR)/check: bench/check.c $(DIR) $(SOS)
	gcc bench/check.c -pthread -O3 -o $(BINDIR)/check


clean:
	rm -rf lib/ obj/ $(SHS) $(BINPATH) tail_*

