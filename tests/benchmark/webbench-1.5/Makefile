CFLAGS?=	-Wall -Wno-unused-parameter -ggdb -W -O
CC?=		gcc
LIBS?=
LDFLAGS?=
PREFIX?=	/usr/local
VERSION=1.5
TMPDIR=/tmp/webbench-$(VERSION)

# Build directories
BUILD_DIR?=	../../../build/tests
BIN_DIR?=	../../../bin

all:   $(BIN_DIR)/webbench

$(BIN_DIR)/webbench: webbench.o Makefile
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ webbench.o $(LIBS)
	@echo "Built: $@"

install: $(BIN_DIR)/webbench
	install -s $(BIN_DIR)/webbench $(DESTDIR)$(PREFIX)/bin 

clean:
	-rm -f *.o *~ core *.core
	-rm -f $(BIN_DIR)/webbench
	
tar:   clean
	-debian/rules clean
	rm -rf $(TMPDIR)
	install -d $(TMPDIR)
	cp -p Makefile webbench.c socket.c webbench.1 $(TMPDIR)
	install -d $(TMPDIR)/debian
	-cp -p debian/* $(TMPDIR)/debian
	ln -sf debian/copyright $(TMPDIR)/COPYRIGHT
	ln -sf debian/changelog $(TMPDIR)/ChangeLog
	-cd $(TMPDIR) && cd .. && tar cozf webbench-$(VERSION).tar.gz webbench-$(VERSION)

webbench.o:	webbench.c socket.c Makefile

.PHONY: clean install all tar
