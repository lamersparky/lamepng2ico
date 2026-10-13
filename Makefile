CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wno-misleading-indentation -Werror
LDFLAGS = -fno-builtin -fno-stack-protector -fno-pie -nostdlib -static -no-pie -Wl,-e,_start -Wl,--build-id=none -Wl,--no-dynamic-linker

.PHONY: all clean install
all: lamepng2ico

lamepng2ico: lamepng2ico.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

install: lamepng2ico
	install -Dm755 lamepng2ico "$(DESTDIR)/usr/local/bin/lamepng2ico"

clean:
	rm -f lamepng2ico
