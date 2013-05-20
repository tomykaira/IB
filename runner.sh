#!/bin/sh
#PBS -j oe
#PBS -l nodes=2:ppn=1

cd ${PBS_O_WORKDIR}
make run
make run2
