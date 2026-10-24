# Spectra3D Script - build with MSYS2 (make)
CC      = C:/msys64/ucrt64/bin/gcc.exe
CFLAGS  = -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-clobbered -static -Isrc -Iinclude
CORE    = src/arena.c src/object.c src/dict.c src/methods.c src/spl.c src/spslib.c \
          src/lexer.c src/parser.c src/compiler.c src/vm.c src/program.c \
          src/builtins.c src/regex.c src/term.c
API     = src/api/api_value.c src/api/api_context.c src/api/api_function.c \
          src/api/api_class.c src/api/api_compiler.c src/api/api_version.c

all: bin/spsc.exe bin/spsvm.exe bin/libsps.a

bin/libsps.a: $(CORE) $(API)
	@mkdir -p bin out
	$(CC) $(CFLAGS) -c $(CORE) $(API)
	$(CC) -r bin/*.o -o bin/libsps.a
	rm -f bin/*.o

bin/spsc.exe: $(CORE) src/api/api_version.c src/main.c
	@mkdir -p bin out
	$(CC) $(CFLAGS) $(CORE) src/api/api_version.c src/main.c -o bin/spsc.exe

bin/spsvm.exe: $(CORE) src/vm_main.c
	@mkdir -p bin out
	$(CC) $(CFLAGS) $(CORE) src/vm_main.c -o bin/spsvm.exe

clean:
	rm -f bin/*.exe out/*.spc

run: bin/spsc.exe
	./bin/spsc.exe examples/hello.sps --run

.PHONY: all clean run
