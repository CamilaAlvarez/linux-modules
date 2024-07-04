IDIR =../../include
CC=gcc
CFLAGS=-I$(IDIR)

ODIR=obj
LDIR =../../lib
LIBS=

_DEPS = mq135-data.h
# patsubst:Finds whitespace-separated words in text(_DEPS) that match pattern(%) and replaces them with replacement($(IDIR)%)
# ‘%’ is replaced by the text that matched the ‘%’ in pattern. (only the first % istreated this way, it can be escaped with \)
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

_OBJ = mq135-user.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

$(ODIR)/%.o: %.c $(DEPS)
	mkdir -p $(ODIR)
	$(CC) -c -o $@ $< $(CFLAGS)

mq135: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~ 