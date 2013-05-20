#include <stdio.h>
#include <stdlib.h>
#include "pmi.h"

#define NMSIZE 1024

#ifndef DEBUG
#if 0
#define DEBUG
#else
#define DEBUG	if(0)
#endif
#endif


extern void	mypmiInit(int *rank, int *procs);
extern void	mypmiPutInt(char *key, int val);
extern int	mypmiGetInt(char *key);
extern void	mypmiPutAddr(char *key, void*);
extern void	*mypmiGetAddr(char *key);
extern void	mypmiPutByte(char *key, char *bytes, int size);
extern void	mypmiGetByte(char *key, char *bytes, int size);

extern void	mypmiBarrier();

