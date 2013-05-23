.PHONY: all queue upload
.SUFFIXES:.c .o .cc

CC     =	gcc
MPICC  =	mpicc
CFLAGS = -O3 -Wall -std=gnu99

SRC    =	ibtest.c ibtest2.c rdma_test.c rdma_tcp.c resource.c qp.c qp_tcp.c \
sendrec.c pmiclient.c comm_tcp.c

all: .depend ibtest ibtest2 rdma_test rdma_tcp queue
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
rdma_test: rdma_test.o resource.o qp.o sendrec.o pmiclient.o
	$(MPICC) -o $@ $^
rdma_tcp: rdma_tcp.o resource.o qp_tcp.o sendrec.o comm_tcp.o
	$(MPICC) -o $@ $^
clean:
	rm -f *.o .depend ibtest ibtest2 rdma_test rdma_tcp

.depend: $(SRC)
	rm -f .depend
	$(CC) -E -MM $(SRC) >> .depend

.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<

include .depend
