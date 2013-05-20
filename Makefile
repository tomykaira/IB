SRC=	ibtest.c resource.c qp.c sendrec.c
CC=	gcc
MPICC=	mpicc
HEADER=	ib.h

.SUFFIXES:.c .o .cc
.c.o:
	$(CC) $<  -c -O3

all: ibtest ibtest2
ibtest: ibtest.o resource.o qp.o sendrec.o
	$(MPICC) -o ibtest ibtest.o resource.o qp.o sendrec.o ../spawn/pmiclient.o -libverbs
ibtest2: ibtest2.o resource.o qp.o sendrec.o
	$(MPICC) -o ibtest2 ibtest2.o resource.o qp.o sendrec.o ../spawn/pmiclient.o -libverbs
clean:
	rm -f *.o ibtest
run:
	mpirun -f mpd.hosts -np 2 ./ibtest
run2:
	mpirun -f mpd.hosts -np 2 ./ibtest2
depend:
	makedepend $(SRC)
# DO NOT DELETE

ibtest.o: ib.h /usr/include/stdio.h /usr/include/features.h
ibtest.o: /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h
ibtest.o: /usr/include/xlocale.h /usr/include/stdlib.h /usr/include/alloca.h
ibtest.o: /usr/include/string.h /usr/include/unistd.h /usr/include/getopt.h
ibtest.o: /usr/include/stdint.h /usr/include/infiniband/verbs.h
ibtest.o: /usr/include/pthread.h /usr/include/endian.h /usr/include/sched.h
ibtest.o: /usr/include/time.h ../spawn/pmiclient.h ../spawn/pmi.h
resource.o: ib.h /usr/include/stdio.h /usr/include/features.h
resource.o: /usr/include/libio.h /usr/include/_G_config.h
resource.o: /usr/include/wchar.h /usr/include/xlocale.h /usr/include/stdlib.h
resource.o: /usr/include/alloca.h /usr/include/string.h /usr/include/unistd.h
resource.o: /usr/include/getopt.h /usr/include/stdint.h
resource.o: /usr/include/infiniband/verbs.h /usr/include/pthread.h
resource.o: /usr/include/endian.h /usr/include/sched.h /usr/include/time.h
qp.o: ib.h /usr/include/stdio.h /usr/include/features.h /usr/include/libio.h
qp.o: /usr/include/_G_config.h /usr/include/wchar.h /usr/include/xlocale.h
qp.o: /usr/include/stdlib.h /usr/include/alloca.h /usr/include/string.h
qp.o: /usr/include/unistd.h /usr/include/getopt.h /usr/include/stdint.h
qp.o: /usr/include/infiniband/verbs.h /usr/include/pthread.h
qp.o: /usr/include/endian.h /usr/include/sched.h /usr/include/time.h
qp.o: ../spawn/pmiclient.h ../spawn/pmi.h
sendrec.o: ib.h /usr/include/stdio.h /usr/include/features.h
sendrec.o: /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h
sendrec.o: /usr/include/xlocale.h /usr/include/stdlib.h /usr/include/alloca.h
sendrec.o: /usr/include/string.h /usr/include/unistd.h /usr/include/getopt.h
sendrec.o: /usr/include/stdint.h /usr/include/infiniband/verbs.h
sendrec.o: /usr/include/pthread.h /usr/include/endian.h /usr/include/sched.h
sendrec.o: /usr/include/time.h
