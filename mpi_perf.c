#include <mpi.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <uuid/uuid.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <assert.h>

#define MAX_HOST_SZ (128)
#define DEF_BUF_SZ (456131)
#define DEF_ITERS (10)
#define LOG_REFRESH_TIME_SEC (900)

int world_size, world_rank, node_local_rank;


int strnicmp(const char *s1, const char *s2, size_t n)
{
    int result = 0;
    for (size_t i = 0; i < n; i++)
    {
        int c1 = tolower((unsigned char)s1[i]);
        int c2 = tolower((unsigned char)s2[i]);

        if (c1 != c2)
        {
            result = c1 - c2;
            break;
        }
        else if (c1 == '\0')
        {
            break;
        }
    }
    return result;
}

#define MPI_CHECK(stmt)                                          \
do {                                                             \
   int mpi_errno = (stmt);                                       \
   if (MPI_SUCCESS != mpi_errno) {                               \
       fprintf(stderr, "[%s:%d] MPI call failed with %d \n",     \
        __FILE__, __LINE__,mpi_errno);                           \
       exit(EXIT_FAILURE);                                       \
   }                                                             \
   assert(MPI_SUCCESS == mpi_errno);                             \
} while (0)


void do_mpi_benchmark_nonblocking(int my_group, int peer_rank,
                      int iters, void *buffer_tx, void *buffer_rx, int buff_len)
{
#define MAX_REQ_NUM 256
    MPI_Status sreqstat[MAX_REQ_NUM];
    MPI_Status rreqstat[MAX_REQ_NUM];
    MPI_Request send_request[MAX_REQ_NUM];
    MPI_Request recv_request[MAX_REQ_NUM];

    int inflight = 0;
    for (int i = 0; i < iters; i++)
    {
        if (my_group == 1)
        {
            MPI_CHECK(MPI_Isend(buffer_tx, buff_len, MPI_CHAR, peer_rank, 1, MPI_COMM_WORLD, &send_request[inflight]));
            MPI_CHECK(MPI_Irecv(buffer_rx, buff_len, MPI_CHAR, peer_rank, 2, MPI_COMM_WORLD, &recv_request[inflight]));
        }
        else
        {
            MPI_CHECK(MPI_Irecv(buffer_rx, buff_len, MPI_CHAR, peer_rank, 1, MPI_COMM_WORLD, &recv_request[inflight]));
            MPI_CHECK(MPI_Isend(buffer_tx, buff_len, MPI_CHAR, peer_rank, 2, MPI_COMM_WORLD, &send_request[inflight]));
        }

        if (inflight == MAX_REQ_NUM - 1)
        {
            MPI_CHECK(MPI_Waitall(inflight, send_request, sreqstat));
            MPI_CHECK(MPI_Waitall(inflight, recv_request, rreqstat));
            inflight = 0;
        }
        else
        {
            inflight++;
        }
    }

    if (inflight > 0)
    {
        MPI_CHECK(MPI_Waitall(inflight, send_request, sreqstat));
        MPI_CHECK(MPI_Waitall(inflight, recv_request, rreqstat));
    }
}


void allocate_tx_rx_buffers(void **buffer_tx, void **buffer_rx, int buff_len, int my_group)
{
    posix_memalign(buffer_tx, 4096, buff_len);
    posix_memalign(buffer_rx, 4096, buff_len);
    if (my_group == 0)
    {
        memset(*buffer_tx, 'a', buff_len);
    }
    else
    {
        memset(*buffer_tx, 'b', buff_len);
    }
}

char group1_hostfile[128] = {0};
int group_size = 0;


void getformatted_time(char *buffer, int for_kusto)
{
    time_t t;
    struct tm *tm_info;

    time(&t);
    tm_info = localtime(&t);

    if (for_kusto)
        strftime(buffer, MAX_HOST_SZ, "%Y-%m-%d %H:%M:%S", tm_info);
    else
        strftime(buffer, MAX_HOST_SZ, "%Y-%m-%d-%H-%M-%S", tm_info);
}


int main(int argc, char **argv)
{
    int my_group = 0;

    MPI_Init(&argc, &argv);

    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    int peer_rank;
    if (world_rank < 8)
    {
	    peer_rank = (world_rank == 7) ? 8 : world_rank + 9;
	    my_group = 1;
    }
    else
    {
	    peer_rank = (world_rank == 8) ? 7 : world_rank - 9;
	    my_group = 0;
    }


    char *local_rank_str = getenv("OMPI_COMM_WORLD_LOCAL_RANK");
    if (local_rank_str == NULL)
    {
	    fprintf(stderr, "failed to read OMPI_COMM_WORLD_LOCAL_RANK\n");
	    MPI_Abort(MPI_COMM_WORLD, -1);
    }
    node_local_rank = atoi(local_rank_str);

     void *buffer_tx, *buffer_rx;
#define MSG_SZ (1048576)
    allocate_tx_rx_buffers(&buffer_tx, &buffer_rx, MSG_SZ, 0);

#define RUN_MAX (2000)
#define ITERS_MAX (2000)

    long double sum_bandwidth = 0.0;
    for (long long int run_idx = 0; run_idx < RUN_MAX; run_idx++ )
    {
        double t_start = 0.0, t_end_local = 0.0, my_time = 0.0;


        MPI_Barrier(MPI_COMM_WORLD);

        t_start = MPI_Wtime();
                    do_mpi_benchmark_nonblocking(my_group, peer_rank,
                            ITERS_MAX, buffer_tx, buffer_rx, MSG_SZ);
        t_end_local = MPI_Wtime();
        my_time = t_end_local - t_start;

#ifdef REPORT_BANDWIDTH
        if (my_group == 0)
        {
            long double gbits_transferred = 8.0 * MSG_SZ * ITERS_MAX * (2.0) * 1e-9;
            long double bandwidth = (gbits_transferred * 1.0) / my_time;
	    sum_bandwidth += bandwidth;
            fprintf(stderr, "[Rank: %d]: Bandwidth: %.2Lf Gbps\n", world_rank, bandwidth);
        }
#endif
    
	if (world_rank < 8)
    {
	    peer_rank = (peer_rank + 1) % 8 + 8;
    }
    else
    {
	    peer_rank = peer_rank == 0 ? 7 : (peer_rank - 1) % 8;
    }

    //fprintf(stderr, "[Rank: %d] peer: %d\n", world_rank, peer_rank);

        MPI_Barrier(MPI_COMM_WORLD);
    }
        if (my_group == 0)
            fprintf(stderr, "[Rank: %d]: Bandwidth: %.2Lf Gbps\n", world_rank, sum_bandwidth / RUN_MAX);

        free(buffer_tx);
        free(buffer_rx);

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
}
