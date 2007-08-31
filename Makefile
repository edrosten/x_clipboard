DFLAGS=
DFLAGS=
OFLAGS=

# the install prefix is overridable with 'make PREFIX=/usr/bin' 
PREFIX = /usr/local


LDFLAGS=-L /usr/X11R6/lib -lX11

CXXFLAGS=$(DFLAGS) $(OFLAGS) -Wall 

CC=$(CXX)

all:paste selection
clean:
	rm -f *.o paste selection


paste:paste.o
	$(CC) -o $@ $^ $(LDFLAGS) $(DFLAGS) $(OFLAGS)

selection:selection.o
	$(CC) -o $@ $^ $(LDFLAGS) $(DFLAGS) $(OFLAGS)

install:
	cp paste selection $(PREFIX)/bin
