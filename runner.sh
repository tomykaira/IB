#!/bin/sh -x
#PBS -l nodes=2:ppn=1
#PBS -j oe

cd ${PBS_O_WORKDIR}

exec > output.$PBS_JOBID 2>&1

me=`hostname`

for host in `cat ${PBS_NODEFILE}`; do
  if [ $host = $me ]; then
    # server
    ./rdma_tcp >server.$PBS_JOBID 2>&1 &
  else
    # client
    sleep 1
    rsh $host "cd ${PBS_O_WORKDIR}; ./rdma_tcp $me > client.$PBS_JOBID 2>&1" &
  fi
done

sleep 4

while [ -n "$(ps -f | grep rdma_tcp)" ]; do
  sleep 10
done

exit 0
