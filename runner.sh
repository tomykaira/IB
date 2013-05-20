#!/bin/sh
#PBS -j oe
#PBS -l nodes=2:ppn=1

cd ${PBS_O_WORKDIR}
mpiexec -np ${PBS_NP} -machinefile ${PBS_NODEFILE} ./ibtest2
exit 0
