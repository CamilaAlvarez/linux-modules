IDIR =../../include
CC=gcc
CFLAGS=-I$(IDIR)

ODIR=lib
LDIR =../../lib
LIBS=

_DEPS = homedomotics-sensors.h
# patsubst:Finds whitespace-separated words in text(_DEPS) that match pattern(%) and replaces them with replacement($(IDIR)%)
# ‘%’ is replaced by the text that matched the ‘%’ in pattern. (only the first % istreated this way, it can be escaped with \)
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

_OBJ = homedomotics-sensors.so
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

$(ODIR)/%.so: %.c $(DEPS)
	mkdir -p $(ODIR)
	$(CC) -shared -o $@ $< $(CFLAGS)

homedomotics-sensors: $(OBJ)
	$(CC) -shared -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f $(ODIR)/*.so *~ core $(INCDIR)/*~
	rm -r $(ODIR)