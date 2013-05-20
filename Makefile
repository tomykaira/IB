.PHONY: all queue upload
.SUFFIXES:.c .o .cc

CC     =	gcc
MPICC  =	mpicc
CFLAGS = -O3 -Wall

SRC    =	ibtest.c resource.c qp.c sendrec.c pmiclient.c

all: .depend ibtest ibtest2 queue
queue: runner.sh
	qsub runner.sh
	qstat
upload:
	rsync -a . csc:IB
	ssh csc 'cd IB; make'

ibtest: ibtest.o resource.o qp.o sendrec.o pmiclient.o
	$(MPICC) -o $@ $^
ibtest2: ibtest2.o resource.o qp.o sendrec.o pmiclient.o
	$(MPICC) -o $@ $^
clean:
	rm -f *.o .depend ibtest ibtest2

.depend: $(SRC)
	rm -f .depend
	$(CC) -E -MM $(SRC) >> .depend

.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<

include .depend
