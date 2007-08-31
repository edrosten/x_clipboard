DFLAGS=
DFLAGS=
OFLAGS=

# the install prefix is overridable with 'make PREFIX=/usr/bin' 
PREFIX = /usr/local

DATADIR=$(PREFIX)/share/x_clipboard/


LDFLAGS=-L /usr/X11R6/lib -lX11

CXXFLAGS=$(DFLAGS) $(OFLAGS) -Wall -DDATADIR=\"$(DATADIR)\"

CC=$(CXX)

all:paste selection
clean:
	rm -f *.o paste selection


paste:paste.o
	$(CC) -o $@ $^ $(LDFLAGS) $(DFLAGS) $(OFLAGS)

selection:selection.o
	$(CC) -o $@ $^ $(LDFLAGS) $(DFLAGS) $(OFLAGS)

install:paste selection
	mkdir -p $(PREFIX)/bin
	cp paste selection $(PREFIX)/bin
	mkdir -p $(DATADIR)
	cp r0x0r.* $(DATADIR)
