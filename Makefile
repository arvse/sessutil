# Session Utility Makefile
INCLUDES=-I include
INDENT_FLAGS=-br -ce -i4 -bl -bli0 -bls -c4 -cdw -ci4 -cs -nbfda -l100 -lp -prs -nlp -nut -nbfde -npsl -nss

OBJS = \
	release/main.o \

all: host

internal: prepare
	@echo "  CC    src/main.c"
	@$(CC) $(CFLAGS) $(INCLUDES) src/main.c -o release/main.o
	@echo "  LD    release/sessutil"
	@$(LD) -o release/sessutil $(OBJS) $(LDFLAGS)

prepare:
	@mkdir -p release

host:
	@make internal \
		CC=gcc \
		LD=gcc \
		CFLAGS='-c -Wall -Wextra -O3 -ffunction-sections -fdata-sections -Wstrict-prototypes' \
		LDFLAGS='-s -Wl,--gc-sections -Wl,--relax -lsqlite3'

install:
	@cp -v release/sessutil /usr/bin/sessutil

uninstall:
	@rm -fv /usr/bin/sessutil

post:
	@echo "  STRIP sessutil"
	@sstrip release/sessutil
	@echo "  UPX   sessutil"
	@upx release/sessutil
	@echo "  LCK   sessutil"
	@perl -pi -e 's/UPX!/EsNf/g' release/sessutil
	@echo "  AEM   sessutil"
	@nogdb release/sessutil

post2:
	@echo "  STRIP sessutil"
	@sstrip release/sessutil
	@echo "  AEM   sessutil"
	@nogdb release/sessutil

indent:
	@indent $(INDENT_FLAGS) ./*/*.h
	@indent $(INDENT_FLAGS) ./*/*.c
	@rm -rf ./*/*~

clean:
	@echo "  CLEAN ."
	@rm -rf release

analysis:
	@scan-build make
	@cppcheck --force */*.h
	@cppcheck --force */*.c

gendoc:
	@doxygen aux/doxygen.conf
