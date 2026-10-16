CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -g -Wall -Wextra
CPPFLAGS += -Ilibmseed -Iflac-1.5.0/include
FLAC_LIB = flac-1.5.0/src/libFLAC/.libs/libFLAC.a
MSEED_SRCS = $(wildcard libmseed/*.c)
MSEED_OBJS = $(MSEED_SRCS:.c=.o)
OBJECTS = $(MSEED_OBJS) mseed_flac.o flac_wrapper.o
.PHONY: all test test-repack test-many-channels test-threaded test-codec clean upstream-tests
all: libmseed_flac_hybrid.a libmseed_flac_hybrid.so test_hybrid repack compare_samples
flac-1.5.0/.configured-static-pic:
	@if test -f flac-1.5.0/Makefile; then $(MAKE) -C flac-1.5.0 distclean; fi
	tar --no-same-owner -xf flac-1.5.0.tar.xz
	cd flac-1.5.0 && ./configure --disable-ogg --disable-cpplibs --disable-doxygen-docs --disable-examples --enable-static --disable-shared --with-pic
	touch $@
$(FLAC_LIB): flac-1.5.0/.configured-static-pic
	$(MAKE) -C flac-1.5.0
%.o: %.c libmseed/libmseed.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@
flac_wrapper.o: $(FLAC_LIB)
mseed_flac.o: mseed_flac.h libmseed/unpack.h
libmseed_flac_hybrid.a: $(OBJECTS) $(FLAC_LIB)
	cp $(FLAC_LIB) $@.tmp
	$(AR) rcs $@.tmp $(OBJECTS)
	mv $@.tmp $@
# Embed the PIC static FLAC archive; no runtime libFLAC dependency.
libmseed_flac_hybrid.so: $(OBJECTS) $(FLAC_LIB)
	$(CC) -shared $(OBJECTS) $(FLAC_LIB) $(LDFLAGS) -lm -o $@
test_hybrid: test_hybrid.c libmseed_flac_hybrid.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmseed_flac_hybrid.a $(LDFLAGS) -lm -o $@
repack: repack.c libmseed_flac_hybrid.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $< libmseed_flac_hybrid.a $(LDFLAGS) -lm -o $@
compare_samples: compare_samples.c libmseed_flac_hybrid.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmseed_flac_hybrid.a $(LDFLAGS) -lm -o $@
test_repack: test_repack.c libmseed_flac_hybrid.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmseed_flac_hybrid.a $(LDFLAGS) -lm -o $@
test_large_headers: test_large_headers.c libmseed_flac_hybrid.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmseed_flac_hybrid.a $(LDFLAGS) -lm -o $@
test_many_channels: test_many_channels.c libmseed_flac_hybrid.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmseed_flac_hybrid.a $(LDFLAGS) -lm -o $@
test: test_hybrid test-repack test_large_headers test-many-channels
	./test_hybrid
	@set -e; task_dir=$$(mktemp -d); trap 'rm -rf "$$task_dir"' EXIT; \
	./test_large_headers "$$task_dir/input.mseed"; \
	./repack "$$task_dir/input.mseed" "$$task_dir/flac.mseed" 21 512; \
	./compare_samples "$$task_dir/input.mseed" "$$task_dir/flac.mseed"
test-threaded: all test_repack test_many_channels
	python3 test_threaded.py
test-codec: test libmseed_flac_hybrid.so
	python3 test_compact.py
	python3 test_codec_io.py
test-many-channels: test_many_channels compare_samples
	@set -e; task_dir=$$(mktemp -d); trap 'rm -rf "$$task_dir"' EXIT; \
	./test_many_channels "$$task_dir/original.mseed" 4000 original; \
	./test_many_channels "$$task_dir/repacked.mseed" 4000 reversed; \
	./test_many_channels "$$task_dir/changed.mseed" 4000 changed; \
	(ulimit -n 32; ./compare_samples "$$task_dir/original.mseed" "$$task_dir/repacked.mseed"); \
	if ./compare_samples "$$task_dir/original.mseed" "$$task_dir/changed.mseed" >"$$task_dir/mismatch.log" 2>&1; then exit 1; fi; \
	grep -q 'Sample mismatch:' "$$task_dir/mismatch.log"; \
	echo 'PASS 4000 channels under 32-descriptor limit, reversed interleaving, genuine sample mismatch detection'
test-repack: test_repack repack compare_samples
	@set -e; task_dir=$$(mktemp -d); trap 'rm -rf "$$task_dir"' EXIT; \
	./test_repack generate "$$task_dir/input.mseed"; \
	./repack "$$task_dir/input.mseed" "$$task_dir/flac.mseed" 21 512; \
	./test_repack verify "$$task_dir/flac.mseed"; \
	./compare_samples "$$task_dir/input.mseed" "$$task_dir/flac.mseed"; \
	./repack "$$task_dir/input.mseed" "$$task_dir/steim2.mseed" 11 512; \
	./test_repack verify "$$task_dir/steim2.mseed"; \
	./compare_samples "$$task_dir/input.mseed" "$$task_dir/steim2.mseed"; \
	./repack "$$task_dir/flac.mseed" "$$task_dir/flac4096.mseed" 21 4096; \
	./compare_samples "$$task_dir/input.mseed" "$$task_dir/flac4096.mseed"; \
	./test_repack generate-continuous "$$task_dir/continuous.mseed"; \
	./repack "$$task_dir/continuous.mseed" "$$task_dir/continuous_flac.mseed" 21 512; \
	./test_repack verify-continuous "$$task_dir/continuous_flac.mseed"; \
	./compare_samples "$$task_dir/continuous.mseed" "$$task_dir/continuous_flac.mseed"; \
	if ./compare_samples "$$task_dir/continuous.mseed" "$$task_dir/input.mseed" >/dev/null 2>&1; then exit 1; fi; \
	head -c 511 "$$task_dir/input.mseed" > "$$task_dir/truncated.mseed"; \
	if ./repack "$$task_dir/truncated.mseed" "$$task_dir/failed.mseed" 21 512 >/dev/null 2>&1; then exit 1; fi; \
	test ! -e "$$task_dir/failed.mseed"; \
	if ./repack "$$task_dir/input.mseed" "$$task_dir/flac.mseed" 21 512 >/dev/null 2>&1; then exit 1; fi; \
	./compare_samples "$$task_dir/input.mseed" "$$task_dir/flac.mseed"
upstream-tests: libmseed_flac_hybrid.a
	cp libmseed_flac_hybrid.a libmseed/libmseed.a
	$(MAKE) -C libmseed/test test LDFLAGS="-L.." LDLIBS="-lmseed -lm"
clean:
	rm -f $(OBJECTS) libmseed_flac_hybrid.a libmseed_flac_hybrid.so test_hybrid test_repack test_large_headers test_many_channels repack compare_samples hybrid_test.mseed
