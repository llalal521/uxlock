include ../Makefile.config

LDFLAGS=-Wl,--whole-archive -Wl,--version-script=interpose.map -Wl,--no-whole-archive  -lrt -lm -ldl -lpapi -pthread
CFLAGS=-I../include/ -fPIC -O3 -g

# Keep objects files
.PRECIOUS: %.o
.SECONDARY: $(OBJS)

.SECONDEXPANSION:
../obj/%.o: $$(lastword $$(subst /, ,%)).c $$(lastword $$(subst /, ,%)).h
	$(eval $@_TMP := $(shell echo $@ | cut -d/ -f3 | cut -d_ -f1))
	$(CC) $(CFLAGS) -D$$(echo $@ | cut -d/ -f3 | cut -d_ -f1 | tr '[a-z]' '[A-Z]') -DCOND_VAR=$(COND_VAR) -DFCT_LINK_SUFFIX=$($@_TMP) -DWAITING_$$(echo $@ | cut -d/ -f3 | cut -d_ -f2- | tr '[a-z]' '[A-Z]') -o $@ -c $<

.SECONDEXPANSION:
../obj/%.o: $$(firstword $$(subst _, , $$(lastword $$(subst /, ,%)))).c ../include/$$(firstword $$(subst _, , $$(lastword $$(subst /, ,%)))).h
	$(eval $@_TMP := $(shell echo $@ | cut -d/ -f3 | cut -d_ -f1))
	$(CC) $(CFLAGS) -D$$(echo $@ | cut -d/ -f3 | cut -d_ -f1 | tr '[a-z]' '[A-Z]') -DCOND_VAR=$(COND_VAR) -DFCT_LINK_SUFFIX=$($@_TMP) -DWAITING_$$(echo $@ | cut -d/ -f3 | cut -d_ -f2- | tr '[a-z]' '[A-Z]') -o $@ -c $<

.SECONDEXPANSION:
../lib/lib%.so: ../obj/%/interpose.o ../obj/%/utils.o $$(subst algo,%,../obj/algo/algo.o)
	$(CC) -shared -o $@ $^ $(LDFLAGS)