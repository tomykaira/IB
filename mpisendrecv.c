#include <mpi.h>
#include <stdio.h>

static inline unsigned long long
getCPUCounter(void)
{
    unsigned int lo, hi;
    __asm__ __volatile__ (      // serialize
	"xorl %%eax,%%eax \n        cpuid"
	::: "%rax", "%rbx", "%rcx", "%rdx");
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (unsigned long long)hi << 32 | lo;
}

#define MHZ	2932.583
//#define SIZE	128
//#define SIZE	1024
#define SIZE	4
#define TIME	10

int nprocs, rank;

main(int argc, char **argv)
{
    char	buf[SIZE];
    float	time;
    unsigned long long	start, end;
    int		i;
    int		ntries = 0;
    MPI_Status	status;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    fprintf(stderr, "[%d] START\n", rank);
    if(rank == 0) {
	memset(buf, 0, SIZE);
	do {
	    MPI_Barrier(MPI_COMM_WORLD);
	    start = getCPUCounter();
	    MPI_Recv(buf, SIZE, MPI_BYTE, 1, 0, MPI_COMM_WORLD, &status);
	    end = getCPUCounter();
	    time = ((float)(end - start))/((float)MHZ);
	    fprintf(stderr, "[%d] %ld clock %f usec\n", rank, (long) (end - start), time);
	} while (ntries++ < TIME);
	start = getCPUCounter();
	for (i = 0; i < TIME; i++) {
	    MPI_Recv(buf, SIZE, MPI_BYTE, 1, 0, MPI_COMM_WORLD, &status);
	    MPI_Send(buf, SIZE, MPI_BYTE, 1, 0, MPI_COMM_WORLD);
	}
	end = getCPUCounter();
	time = (((float)(end - start))/((float)MHZ))/TIME;
	fprintf(stderr, "[%d] %ld clock %f usec\n", rank, (long) (end - start), time);
    } else {
	for (i = 0; i < SIZE; i++) buf[i] = i;
	do {
	    MPI_Barrier(MPI_COMM_WORLD);
	    start = getCPUCounter();
	    MPI_Send(buf, SIZE, MPI_BYTE, 0, 0, MPI_COMM_WORLD);
	    end = getCPUCounter();
	    time = ((float)(end - start))/((float)MHZ);
	    fprintf(stderr, "[%d] %ld clock %f usec\n", rank, (long) (end - start), time);
	} while (ntries++ < TIME);
	start = getCPUCounter();
	for (i = 0; i < TIME; i++) {
	    MPI_Send(buf, SIZE, MPI_BYTE, 0, 0, MPI_COMM_WORLD);
	    MPI_Recv(buf, SIZE, MPI_BYTE, 0, 0, MPI_COMM_WORLD, &status);
	}
	end = getCPUCounter();
	time = (((float)(end - start))/((float)MHZ))/TIME;
	fprintf(stderr, "[%d] %ld clock %f usec\n", rank, (long) (end - start), time);
    }
    MPI_Finalize();
    return 0;
}
