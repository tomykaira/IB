#!/bin/sh
#PBS -l nodes=2:ppn=1
#PBS -o /dev/null
#PBS -e /dev/null

cd ${PBS_O_WORKDIR}

exec > output.$PBS_JOBNAME 2>&1

me=`hostname`

for host in `cat ${PBS_NODEFILE}`; do
  if [ $host = $me ]; then
    # server
    ./rdma_tcp &
  else
    # client
    sleep 1
    rsh $host "cd ${PBS_O_WORKDIR} ./rdma_tcp $me" &
  fi
done

sleep 4

while [ -n "$(ps -f | grep rdma_tcp)" ]; do
  sleep 10
done

exit 0
