OBJ=rs.o deps/obl/oblas_common.o deps/obl/oblas_lite.o rs16.o deps/obl/oblas16.o rs16_afft.o deps/obl/oblas16_afft.o

TEST_UTILS=\
	t/00util/test\
	t/00util/bench\
	t/00util/test16\
	t/00util/test16_afft\
	t/00util/bench16\
	t/00util/bench16_afft

CFLAGS   = -O3 -g -std=c11 -Wall -I. -Ideps/obl
CFLAGS  += -march=native -funroll-loops -ftree-vectorize

all: $(OBJ)

t/00util/test.o: CPPFLAGS+=-D_DEFAULT_SOURCE

t/00util/bench.o: CPPFLAGS+=-D_DEFAULT_SOURCE

t/00util/test16.o: CPPFLAGS+=-D_DEFAULT_SOURCE

t/00util/test16_afft.o: CPPFLAGS+=-D_DEFAULT_SOURCE

t/00util/bench16.o: CPPFLAGS+=-D_DEFAULT_SOURCE

t/00util/bench16_afft.o: CPPFLAGS+=-D_DEFAULT_SOURCE

t/00util/test: t/00util/test.o $(OBJ)

t/00util/bench: t/00util/bench.o $(OBJ)

t/00util/test16: t/00util/test16.o $(OBJ)

t/00util/test16_afft: t/00util/test16_afft.o $(OBJ)

t/00util/bench16: t/00util/bench16.o $(OBJ)

t/00util/bench16_afft: t/00util/bench16_afft.o $(OBJ)

RUN ?=

check: clean $(TEST_UTILS) check-vectorization
	RUN="$(RUN)" prove -I. -v t/*.t
	$(RUN) ./t/00util/test16
	$(RUN) ./t/00util/test16_afft

check-vectorization:
	@echo "Checking for autovectorization..."
	@if $(CC) $(CPPFLAGS) $(CFLAGS) -dM -E - < /dev/null 2>&1 | grep -q "__riscv"; then \
		echo "Skipping autovectorization check on RISC-V (using manual RVV intrinsics)."; \
	else \
		(($(CC) $(CPPFLAGS) $(CFLAGS) -fopt-info-vec -c deps/obl/oblas_lite.c -o /dev/null 2>&1 | grep -q "vectorized") || \
		 ($(CC) $(CPPFLAGS) $(CFLAGS) -Rpass=loop-vectorize -c deps/obl/oblas_lite.c -o /dev/null 2>&1 | grep -q "vectorized") || \
		 (echo "ERROR: Loop was not autovectorized!" && exit 1)); \
	fi

clean:
	$(RM) *.o *.a $(TEST_UTILS) $(OBJ) t/00util/*.o deps/obl/*.o

indent:
	find -name '*.[h,c]' | xargs clang-format -i

scan: 
	scan-build --status-bugs $(MAKE) clean $(OBJ) $(TEST_UTILS)

valgrind: CFLAGS = -O0 -g -std=c11 -Wall -I. -Ideps/obl
valgrind: clean $(TEST_UTILS)
	valgrind --error-exitcode=2 ./t/00util/bench 200 20 512

gperf: LDLIBS = -lprofiler -ltcmalloc
gperf: clean t/00util/bench
	CPUPROFILE_FREQUENCY=100000000 CPUPROFILE=gperf.prof ./t/00util/bench 200 20 512
	pprof ./t/00util/bench gperf.prof --callgrind > callgrind.gperf
	gprof2dot --format=callgrind callgrind.gperf -z main | dot -T svg > gperf.svg

check-asan: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
check-asan: LDLIBS += -fsanitize=address,undefined
check-asan: clean $(TEST_UTILS)
	RUN="$(RUN)" prove -I. -v t/*.t
	$(RUN) ./t/00util/test16
