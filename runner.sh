#!/bin/sh -x
#PBS -l nodes=2:ppn=1
#PBS -j oe

cd ${PBS_O_WORKDIR}

mpiexec -np ${PBS_NP} -machinefile ${PBS_NODEFILE} ./ibtest
# /home/tomykaira/.rbenv/shims/ruby runner.rb
exit 0
