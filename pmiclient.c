#include "pmiclient.h"
#include <string.h>

static char kvsname[NMSIZE];
static int myrank;
static int nprocs;

void
mypmiInit(int *rk, int *psize)
{
    int		cc;
    int		spawned;
    if ((cc=PMI_Init(&spawned)) != PMI_SUCCESS) {
	fprintf(stderr, "Cannot initialize PMI (cc=%d)\n", cc);
	exit(-1);
    }
    if ((cc=PMI_Get_size(&nprocs)) != PMI_SUCCESS) {
	fprintf(stderr, "[--] Cannot obtain the number of processes (cc=%d)\n", cc);
	goto error;
    }
    if ((cc=PMI_Get_rank(&myrank)) != PMI_SUCCESS) {
	fprintf(stderr, "[--] Cannot obtain my rank (cc=%d)\n", cc);
	goto error;
    }
    *rk = myrank; *psize = nprocs;
    if ((cc=PMI_KVS_Get_my_name(kvsname, NMSIZE)) != PMI_SUCCESS) {
	fprintf(stderr, "[%d] Cannot obtain my keyval space name (cc=%d)\n", myrank, cc);
	goto error;
    }
    DEBUG {
	fprintf(stderr, "[%d] keyval space (%s) size(%d) rank(%d)\n",
		myrank, kvsname, nprocs, myrank);
    }
    return;
error:
    PMI_Finalize();
    exit(-1);
}

void
mypmiPutInt(char *key, int val)
{
    int		cc;
    char	value[NMSIZE];

    sprintf(value, "%d", val);
    if ((cc = PMI_KVS_Put(kvsname, key, value)) != PMI_SUCCESS) {
	fprintf(stderr, "[%d] Cannot put the key(%s) (cc=%d)\n", myrank, key,  cc);
    }
}

void
mypmiPutAddr(char *key, void *val)
{
    int		cc;
    char	value[NMSIZE];

    sprintf(value, "%p", val);
    if ((cc = PMI_KVS_Put(kvsname, key, value)) != PMI_SUCCESS) {
	fprintf(stderr, "[%d] Cannot put the key(%s) (cc=%d)\n", myrank, key,  cc);
    }
}

void
mypmiPutByte(char *key, char *bytes, int size)
{
    int		cc;
    int		i;
    char	bb[8];
    char	value[NMSIZE];

    value[0] = 0;
    for (i = 0; i < size; i++) {
	sprintf(bb, "0x%02x:", (unsigned char)bytes[i]);
	strcat(value, bb);
    }
    if ((cc = PMI_KVS_Put(kvsname, key, value)) != PMI_SUCCESS) {
	fprintf(stderr, "[%d] Cannot put the key(%s) (cc=%d)\n", myrank, key,  cc);
    }
}

void
mypmiGetByte(char *key, char *bytes, int size)
{
    int		cc;
    int		i;
    int		d;
    char	tmp[NMSIZE];
    char	*cp;

    if ((cc = PMI_KVS_Get(kvsname, key, tmp, NMSIZE)) != PMI_SUCCESS) {
	fprintf(stderr, "[%d] Cannot get the key(%s) (cc=%d)\n", myrank, key,  cc);
    }
    cp = tmp;
    for (i = 0; i < size; i++) {
	sscanf(cp, "%x", &d);
	bytes[i] = (char) d;
	cp = index(cp, ':');
	if (cp == 0) break;
	cp++;
    }
}

int
mypmiGetInt(char *key)
{
    
    int		cc;
    char	tmp[NMSIZE];

    if ((cc = PMI_KVS_Get(kvsname, key, tmp, NMSIZE)) != PMI_SUCCESS) {
	fprintf(stderr, "[%d] Cannot get the key(%s) (cc=%d)\n", myrank, key,  cc);
    }
    return atoi(tmp);
}

void*
mypmiGetAddr(char *key)
{
    
    int		cc;
    void	*addr;
    char	tmp[NMSIZE];

    if ((cc = PMI_KVS_Get(kvsname, key, tmp, NMSIZE)) != PMI_SUCCESS) {
	fprintf(stderr, "[%d] Cannot get the key(%s) (cc=%d)\n", myrank, key,  cc);
    }
    sscanf(tmp, "%p", &addr);
    return addr;
}

void
mypmiBarrier()
{
    PMI_Barrier();
}
