#!/bin/sh -x
#PBS -l nodes=2:ppn=1
#PBS -j oe

cd ${PBS_O_WORKDIR}

/home/tomykaira/.rbenv/shims/ruby runner.rb
exit 0
