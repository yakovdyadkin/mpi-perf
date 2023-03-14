mpi_perf: mpi_perf.c
	mpicc -Wall -luuid -o mpi_perf mpi_perf.c
clean:
	rm -rf mpi_perf
