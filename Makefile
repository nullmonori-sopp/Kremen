CC ?= cc
CFLAGS ?= -O3
STRICT = -std=c11 -Wall -Wextra -Wpedantic -Werror
shell_quote = '$(subst ','"'"',$(1))'
CC_COMMAND = $(call shell_quote,$(CC))
HOST_OS := $(shell uname -s)
ARCH := $(shell uname -m)
USE_ASM ?= 1
EXEEXT =
ifneq (,$(filter MINGW% MSYS%,$(HOST_OS)))
EXEEXT = .exe
endif
COPY = native/fast_copy.c
ifeq ($(HOST_OS),Linux)
ifeq ($(ARCH),x86_64)
ifeq ($(USE_ASM),1)
COPY = native/fast_copy.S
endif
endif
endif
SOURCES = native/core.c native/diagnostics.c native/doctor.c native/lexer.c native/parser.c native/modules.c native/checker.c native/learn.c native/codegen.c native/format.c native/main.c

.PHONY: all test test-sanitize test-thread-sanitize test-install test-selfhost check examples bootstrap selfhost bench dist clean FORCE
all: build/kremen$(EXEEXT) build/kremen-backend$(EXEEXT)

build/config: FORCE
	mkdir -p build
	@printf '%s\n' $(call shell_quote,$(CC) $(STRICT) $(CFLAGS) $(COPY)) > build/config.tmp
	@cmp -s build/config.tmp build/config || cp build/config.tmp build/config
	@rm -f build/config.tmp

build/kremen$(EXEEXT): $(SOURCES) $(COPY) native/kremen.h native/platform.h selfhost/platform.h runtime/kremen_rt.h build/config
	mkdir -p build
	$(CC_COMMAND) $(STRICT) $(CFLAGS) $(SOURCES) $(COPY) -o $@

build/kremen-backend$(EXEEXT): tools/backend_launcher.c native/platform.h native/kremen.h selfhost/platform.h build/config
	mkdir -p build
	$(CC_COMMAND) $(STRICT) $(CFLAGS) tools/backend_launcher.c -o $@

.PHONY: test-windows
test-windows: all
	sh tests/windows/check.sh ./build/kremen$(EXEEXT)

test: build/kremen$(EXEEXT)
	sh tests/native/run.sh ./build/kremen$(EXEEXT)
	sh tests/native/v3.sh ./build/kremen$(EXEEXT)
	sh tests/native/frontend_stability.sh ./build/kremen$(EXEEXT)
	sh tests/native/semantic_stability.sh ./build/kremen$(EXEEXT)
	sh tests/native/runtime_stability.sh
	sh tests/native/toolchain_stability.sh ./build/kremen$(EXEEXT)
	sh tests/native/diagnostics.sh ./build/kremen$(EXEEXT)
	sh tests/native/diagnostics_043.sh ./build/kremen$(EXEEXT)
	sh tests/native/checker_codegen_043.sh ./build/kremen$(EXEEXT)
	sh tests/native/stabilization_044.sh ./build/kremen$(EXEEXT)
	sh tests/native/profiles_045.sh ./build/kremen$(EXEEXT)
	sh tests/native/security_045.sh ./build/kremen$(EXEEXT)
	sh tests/native/malformed_045.sh ./build/kremen$(EXEEXT)
	sh tests/native/checker_046.sh ./build/kremen$(EXEEXT)
	sh tests/native/codegen_046.sh ./build/kremen$(EXEEXT)
	sh tests/native/input_046.sh ./build/kremen$(EXEEXT)
	sh tests/native/output_046.sh ./build/kremen$(EXEEXT)
	sh tests/native/output_runtime_046.sh ./build/kremen$(EXEEXT)
	sh tests/native/semantic_047.sh ./build/kremen$(EXEEXT)
	sh tests/native/runtime_047.sh
	sh tests/native/doctor_047.sh ./build/kremen$(EXEEXT)
	sh tests/native/runtime_048.sh
	sh tests/native/backend_048.sh
	sh tests/native/threads_043.sh ./build/kremen$(EXEEXT)
	sh tests/native/threads_runtime.sh
	sh tests/native/tutorial.sh ./build/kremen$(EXEEXT)
	$(MAKE) test-selfhost
	sh tests/native/source_io.sh

test-sanitize:
	sh tests/selfhost/sanitize.sh
	sh tests/native/sanitize.sh
	KREMEN_SELFHOST_ROOT="$(CURDIR)/selfhost" sh tests/native/source_io.sh "$(CURDIR)/build/sanitize/kremen" "$(CURDIR)/build/sanitize-selfhost/stage0" "$(CURDIR)/build/sanitize-selfhost/kremen-selfhost"

test-thread-sanitize:
	SANITIZE=thread TSAN_OPTIONS=halt_on_error=1 sh tests/native/threads_runtime.sh

test-install: build/kremen$(EXEEXT)
	sh tests/native/install.sh

dist:
	sh tools/package.sh

check: build/kremen$(EXEEXT)
	./build/kremen$(EXEEXT) check examples/demo.kr

examples: build/kremen$(EXEEXT)
	./build/kremen$(EXEEXT) run examples/hello.kr
	./build/kremen$(EXEEXT) build examples/demo.kr -o build/demo
	./build/demo
	./build/kremen$(EXEEXT) run examples/errors.kr
	./build/kremen$(EXEEXT) run examples/slices.kr
	./build/kremen$(EXEEXT) run examples/modules/main.kr
	./build/kremen$(EXEEXT) run examples/algebra.kr
	./build/kremen$(EXEEXT) run examples/resources.kr
	./build/kremen$(EXEEXT) run examples/threads.kr
	printf '4\n' | ./build/kremen$(EXEEXT) run examples/learn/checked_input.kr --mode=learn

bootstrap:
	sh selfhost/bootstrap.sh

selfhost: selfhost/build/kremen-selfhost$(EXEEXT)

selfhost/build/kremen-selfhost$(EXEEXT): selfhost/bootstrap.kr selfhost/toolchain.kr selfhost/stage0.c selfhost/runtime.c selfhost/runtime.h selfhost/driver.c selfhost/host.c selfhost/host.h selfhost/platform.h selfhost/bootstrap.sh native/platform.h
	sh selfhost/bootstrap.sh

test-selfhost: selfhost/build/kremen-selfhost$(EXEEXT)
	sh tests/selfhost/semantics.sh
	sh tests/selfhost/toolchain.sh
	sh tests/selfhost/frontend_stability.sh
	sh tests/selfhost/runtime_stability.sh
	sh tests/selfhost/diagnostics.sh
	sh tests/selfhost/output_048.sh

bench: build/kremen$(EXEEXT)
	$(CC_COMMAND) $(STRICT) $(CFLAGS) bench/copy.c $(COPY) -o build/bench-copy
	./build/bench-copy

clean:
	rm -rf build selfhost/build
