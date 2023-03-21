#!/bin/bash

ITERS=5000
RUNS=10
FLOWS=1
#BUFF_SZ=8388608
#BUFF_SZ=4194304
#BUFF_SZ=41943040
BUFF_SZ=4194304

LOGFOLDER=/mnt/anfvol/jijos/mpi-perf
HOSTFILE=/mnt/anfvol/jijos/mpi-perf/hostfile
NUM_HOSTS=2

GROUP1FILE=/mnt/anfvol/jijos/mpi-perf/group1
NUM_GROUP1=1

BINARY=/mnt/anfvol/jijos/mpi-perf/mpi_perf


NUM_PROCS=$((NUM_HOSTS * FLOWS))

#Runs between LL02 and LL03
mpirun -np ${NUM_PROCS} -hostfile ${HOSTFILE} --map-by ppr:${FLOWS}:node \
	-mca plm_rsh_no_tree_spawn 1 -mca plm_rsh_num_concurrent 800 \
	-x LD_LIBRARY_PATH -x UCX_NET_DEVICES=mlx5_ib0:1 -x UCX_TLS=rc \
	numactl --cpunodebind=0 --membind 0 \
	$BINARY -f ${GROUP1FILE} -n ${NUM_GROUP1} -p ${FLOWS} -r ${RUNS} -i ${ITERS} -b ${BUFF_SZ} -l ${LOGFOLDER} -x 1
