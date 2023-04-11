mpi_perf: mpi_perf.c
	mpicc -Wall -o mpi_perf mpi_perf.c -luuid
clean:
	rm -rf mpi_perf
