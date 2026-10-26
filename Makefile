CC ?= cc
CFLAGS ?= -O3
STRICT = -std=c11 -Wall -Wextra -Wpedantic -Werror
shell_quote = '$(subst ','"'"',$(1))'
CC_COMMAND = $(call shell_quote,$(CC))
HOST_OS := $(shell uname -s)
ARCH := $(shell uname -m)
USE_ASM ?= 1
COPY = native/fast_copy.c
ifeq ($(HOST_OS),Linux)
ifeq ($(ARCH),x86_64)
ifeq ($(USE_ASM),1)
COPY = native/fast_copy.S
endif
endif
endif
SOURCES = native/core.c native/lexer.c native/parser.c native/modules.c native/checker.c native/codegen.c native/format.c native/main.c

.PHONY: all test test-sanitize test-thread-sanitize test-install test-selfhost check examples bootstrap selfhost bench dist clean FORCE
all: build/kremen

build/config: FORCE
	mkdir -p build
	@printf '%s\n' $(call shell_quote,$(CC) $(STRICT) $(CFLAGS) $(COPY)) > build/config.tmp
	@cmp -s build/config.tmp build/config || cp build/config.tmp build/config
	@rm -f build/config.tmp

build/kremen: $(SOURCES) $(COPY) native/kremen.h selfhost/platform.h runtime/kremen_rt.h build/config
	mkdir -p build
	$(CC_COMMAND) $(STRICT) $(CFLAGS) $(SOURCES) $(COPY) -o $@

test: build/kremen
	sh tests/native/run.sh ./build/kremen
	sh tests/native/v3.sh ./build/kremen
	sh tests/native/frontend_stability.sh ./build/kremen
	sh tests/native/semantic_stability.sh ./build/kremen
	sh tests/native/runtime_stability.sh
	sh tests/native/toolchain_stability.sh ./build/kremen
	sh tests/native/diagnostics.sh ./build/kremen
	sh tests/native/diagnostics_043.sh ./build/kremen
	sh tests/native/checker_codegen_043.sh ./build/kremen
	sh tests/native/threads_043.sh ./build/kremen
	sh tests/native/threads_runtime.sh
	sh tests/native/tutorial.sh ./build/kremen
	$(MAKE) test-selfhost
	sh tests/native/source_io.sh

test-sanitize:
	sh tests/selfhost/sanitize.sh
	sh tests/native/sanitize.sh
	KREMEN_SELFHOST_ROOT="$(CURDIR)/selfhost" sh tests/native/source_io.sh "$(CURDIR)/build/sanitize/kremen" "$(CURDIR)/build/sanitize-selfhost/stage0" "$(CURDIR)/build/sanitize-selfhost/kremen-selfhost"

test-thread-sanitize:
	SANITIZE=thread TSAN_OPTIONS=halt_on_error=1 sh tests/native/threads_runtime.sh

test-install: build/kremen
	sh tests/native/install.sh

dist:
	sh tools/package.sh

check: build/kremen
	./build/kremen check examples/demo.kr

examples: build/kremen
	./build/kremen run examples/hello.kr
	./build/kremen build examples/demo.kr -o build/demo
	./build/demo
	./build/kremen run examples/errors.kr
	./build/kremen run examples/slices.kr
	./build/kremen run examples/modules/main.kr
	./build/kremen run examples/algebra.kr
	./build/kremen run examples/resources.kr
	./build/kremen run examples/threads.kr

bootstrap:
	sh selfhost/bootstrap.sh

selfhost: selfhost/build/kremen-selfhost

selfhost/build/kremen-selfhost: selfhost/bootstrap.kr selfhost/toolchain.kr selfhost/stage0.c selfhost/runtime.c selfhost/runtime.h selfhost/driver.c selfhost/host.c selfhost/host.h selfhost/platform.h selfhost/bootstrap.sh
	sh selfhost/bootstrap.sh

test-selfhost: selfhost/build/kremen-selfhost
	sh tests/selfhost/semantics.sh
	sh tests/selfhost/toolchain.sh
	sh tests/selfhost/frontend_stability.sh
	sh tests/selfhost/runtime_stability.sh
	sh tests/selfhost/diagnostics.sh

bench: build/kremen
	$(CC_COMMAND) $(STRICT) $(CFLAGS) bench/copy.c $(COPY) -o build/bench-copy
	./build/bench-copy

clean:
	rm -rf build selfhost/build
