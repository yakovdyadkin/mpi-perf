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

int world_size, world_rank;

void print_usage()
{
    fprintf(stderr, "Usage: <program> \n\
		    -f <group1-hosts>\n\
		    -n <group1-size> \n\
		    -d <use-dotnet 0|1>\n\
		    -p <ppn> \n -i <iters>\n\
		    -b <buffer-size>\n\
		    -u <uni-directional (MPI-only) 0|1>\n\
		    -r <number-of-runs>\n\
		    -l <logfolder>\n\
		    -x <use non-blocking MPI calls>");
}

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

void do_mpi_benchmark(int my_group, int my_rank, int peer_rank, char *peer_host, char *my_host,
                      int iters, void *buffer_tx, void *buffer_rx, int buff_len)
{
    MPI_Status status;
    for (int i = 0; i < iters; i++)
    {
        if (my_group == 1)
        {
            MPI_CHECK(MPI_Send(buffer_tx, buff_len, MPI_CHAR, peer_rank, 1, MPI_COMM_WORLD));
            MPI_CHECK(MPI_Recv(buffer_rx, buff_len, MPI_CHAR, peer_rank, 2, MPI_COMM_WORLD, &status));
        }
        else
        {
            MPI_CHECK(MPI_Recv(buffer_rx, buff_len, MPI_CHAR, peer_rank, 1, MPI_COMM_WORLD, &status));
            MPI_CHECK(MPI_Send(buffer_tx, buff_len, MPI_CHAR, peer_rank, 2, MPI_COMM_WORLD));
        }
    }
}

void do_mpi_benchmark_nonblocking(int my_group, int my_rank, int peer_rank, char *peer_host, char *my_host,
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

void do_mpi_benchmark_unidir(int my_group, int my_rank, int peer_rank, char *peer_host, char *my_host,
                             int iters, void *buffer_tx, void *buffer_rx, int buff_len)
{
    MPI_Status status;

    for (int i = 0; i < iters; i++)
    {
        if (my_group == 1)
        {
            MPI_CHECK(MPI_Send(buffer_tx, buff_len, MPI_CHAR, peer_rank, 1, MPI_COMM_WORLD));
            MPI_CHECK(MPI_Recv(buffer_rx, 1, MPI_CHAR, peer_rank, 2, MPI_COMM_WORLD, &status));
        }
        else
        {
            MPI_CHECK(MPI_Recv(buffer_rx, buff_len, MPI_CHAR, peer_rank, 1, MPI_COMM_WORLD, &status));
            MPI_CHECK(MPI_Send(buffer_tx, 1, MPI_CHAR, peer_rank, 2, MPI_COMM_WORLD));
        }
    }
}

void do_launch_dotnet_bench(int my_group, int my_rank, int peer_rank, char *peer_ipaddr, char *my_ipaddr,
                             int buff_len, int iters, long long int run_idx, int ppn)
{
#define DEF_PORT (40000)
    char command[1024] = {0};

    if (my_group == 1)
    {
        sprintf(command, "dotnet /mnt/anfvol/tepati/clientserverapp/bin/Release/net6.0/clientserverapp.dll server %s %d 1 %d %d %d %d true",
                my_ipaddr, DEF_PORT + my_rank, ppn, buff_len, iters, 0);
        //system(command);
        fprintf(stderr, "%s\n", command);
    }
    else
    {
        sprintf(command, "dotnet /mnt/anfvol/tepati/clientserverapp/bin/Release/net6.0/clientserverapp.dll client %s %d %d %d %d %d true",
                peer_ipaddr, DEF_PORT + peer_rank, ppn, buff_len, iters, 0);
        //system(command);
        fprintf(stderr, "%s\n", command);

    }
}


void get_ipaddress(char *hostname, char *ipstr)
{
    struct addrinfo hints, *res;
    int status;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(hostname, NULL, &hints, &res)) != 0)
    {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        MPI_Abort(MPI_COMM_WORLD, -1);
    }

    void *addr;
    // loop through all the results and get the address
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next)
    {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
        addr = &(ipv4->sin_addr);

        // convert the IP to a string and print it:
        inet_ntop(p->ai_family, addr, ipstr, INET_ADDRSTRLEN);
    }

    freeaddrinfo(res); // free the linked list
}

void get_peer_rank(int my_group, int group_rank, char *myhostname, int *my_peer, char **my_peer_host,
        char *my_ipaddr, char *peer_ipaddr)
{
    // do allgather to find out the peers
    struct node_info
    {
        int group_id;
        int group_rank;
        char hostname[MAX_HOST_SZ];
    };

    // identify peer node
    *my_peer = -1;
    *my_peer_host = NULL;

    struct node_info my_node_info={0};
    my_node_info.group_id = my_group;
    my_node_info.group_rank = group_rank;
    memcpy(my_node_info.hostname, myhostname, strlen(myhostname));

    struct node_info *world_node_info = (struct node_info *)malloc(sizeof(struct node_info) * world_size);
    memset(world_node_info, 0, sizeof(struct node_info) * world_size);

    MPI_Allgather(&my_node_info, sizeof(struct node_info), MPI_BYTE,
                  world_node_info, sizeof(struct node_info), MPI_BYTE, MPI_COMM_WORLD);
    for (int i = 0; i < world_size; i++)
    {
        struct node_info *info = (struct node_info *)world_node_info + i;
        if (info->group_id != my_group && info->group_rank == group_rank)
        {
            *my_peer = i;
            *my_peer_host = info->hostname;
            break;
        }
    }

    get_ipaddress(myhostname, my_ipaddr);
    get_ipaddress(*my_peer_host, peer_ipaddr);
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

struct options
{
    int use_dotnet;
    int iters;
    int buff_sz;
    int uni_dir;
    int num_runs;
    int ppn;
    int nonblocking;
    char uuid[64];
    char logfolder[MAX_HOST_SZ];
};

struct options bench_options = {0};
FILE *log_fp = NULL;

void parse_args(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, ":f:n:d:p:i:b:u:h:r:l:x:")) != -1)
    {
        switch (opt)
        {
        case 'f':
            // group1 hostnames
            strncpy(group1_hostfile, optarg, MAX_HOST_SZ);
            break;

        // no. of hosts in group1
        case 'n':
            group_size = (int)atoi(optarg);
            break;

        // use dotnet for benchmarking
        case 'd':
            bench_options.use_dotnet = (int)atoi(optarg);
            break;

        // specify processes per node (PPN)
        case 'p':
            bench_options.ppn = (int)atoi(optarg);
            break;

        // iteration count
        case 'i':
            bench_options.iters = (int)atoi(optarg);
            break;

        // buffer size in MB
        case 'b':
            bench_options.buff_sz = (int)atoi(optarg);
            break;

        // uni-directional benchmark
        case 'u':
            bench_options.uni_dir = (int)atoi(optarg);
            break;

        // number of runs
        case 'r':
            bench_options.num_runs = (int)atoi(optarg);
            break;

        // number of runs
        case 'x':
            bench_options.nonblocking = (int)atoi(optarg);
            break;

	case 'l':
	    strncpy(bench_options.logfolder, optarg, MAX_HOST_SZ);
	    break;

        default:
            print_usage();
            MPI_Abort(MPI_COMM_WORLD, -1);
        }
    }

    uuid_t uuid;
    uuid_generate(uuid);
    uuid_unparse(uuid, &bench_options.uuid[0]);
    fprintf(stderr, "UUID: %s\n", bench_options.uuid);
}

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
    int i = 0;
    int my_group = 0, group_rank = 0;

    MPI_Init(&argc, &argv);
    MPI_Comm group_comm;

    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    char *group1_hostnames = NULL;

    bench_options.use_dotnet = 0;
    bench_options.uni_dir = 0;
    bench_options.iters = DEF_ITERS;
    bench_options.buff_sz = DEF_BUF_SZ;
    bench_options.num_runs = 1;

    if (world_rank == 0)
    {
        parse_args(argc, argv);

        // validate group_size
        if (group_size <= 0 || (!bench_options.uni_dir && group_size != world_size / (2 * bench_options.ppn)))
        {
            fprintf(stderr, "invalid group_size: %d, world_size: %d, ppn: %d\n", group_size, world_size, bench_options.ppn);
            MPI_Abort(MPI_COMM_WORLD, -1);
        }

        // read group1 hostnames
        group1_hostnames = (char *)malloc(group_size * MAX_HOST_SZ);
        memset(group1_hostnames, 0, group_size * MAX_HOST_SZ);

        FILE *fptr = NULL;
        fptr = fopen(group1_hostfile, "r");
        if (fptr == NULL)
        {
            fprintf(stderr, "cannot open group1 file: %s\n", group1_hostfile);
            MPI_Abort(MPI_COMM_WORLD, -1);
        }

        while (fgets(group1_hostnames + i * MAX_HOST_SZ, MAX_HOST_SZ, fptr))
            i++;
    }

    // broadcase benchmark options
    MPI_Bcast(&bench_options, sizeof(bench_options), MPI_CHAR, 0, MPI_COMM_WORLD);

    // broadcast group1 hosts info to all other processes
    MPI_Bcast(&group_size, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (world_rank != 0)
    {
        group1_hostnames = (char *)malloc(group_size * MAX_HOST_SZ);
        memset(group1_hostnames, 0, group_size * MAX_HOST_SZ);
    }
    MPI_Bcast(group1_hostnames, group_size * MAX_HOST_SZ, MPI_CHAR, 0, MPI_COMM_WORLD);

    int name_len;
    char myhostname[MAX_HOST_SZ] = {0};
    MPI_Get_processor_name(myhostname, &name_len);

    // identify if i am in group1 or not
    for (int i = 0; i < group_size; i++)
    {
        if (strnicmp(myhostname, group1_hostnames + i * MAX_HOST_SZ, name_len) == 0)
        {
            my_group = 1;
        }
    }

    // Create a new communicator consisting of processes with the same group
    MPI_Comm_split(MPI_COMM_WORLD, my_group, world_rank, &group_comm);

    MPI_Comm_size(group_comm, &group_size);
    MPI_Comm_rank(group_comm, &group_rank);

    // identify peer node
    int my_peer = -1;
    char *my_peer_host = NULL;
    char my_ipaddr[MAX_HOST_SZ] = {0};
    char peer_ipaddr[MAX_HOST_SZ] = {0};

    get_peer_rank(my_group, group_rank, (char *)myhostname, &my_peer, &my_peer_host, my_ipaddr, peer_ipaddr);

    fprintf(stderr, "INFO: %s, rank %d out of %d ranks, my_group: %d, group_size: %d, group_rank: %d, my_peer: %d, hostname: %s (%s), peer_host: %s (%s)\n",
            myhostname, world_rank, world_size, my_group, group_size, group_rank, my_peer, myhostname, my_ipaddr, my_peer_host, peer_ipaddr);

    void *buffer_tx, *buffer_rx;
    int buff_len = bench_options.buff_sz;
    if (!bench_options.use_dotnet)
    {
        allocate_tx_rx_buffers(&buffer_tx, &buffer_rx, buff_len, my_group);
    }

    // core benchmark
    double t_last_logtime = 0.0;
    t_last_logtime = MPI_Wtime();

    for (long long int run_idx = 0; bench_options.num_runs == -1 || run_idx < bench_options.num_runs; run_idx++ )
    {
        double t_start = 0.0, t_end = 0.0, t_end_local = 0.0;
        double my_time, min_time, max_time, sum_time;

        if (log_fp == NULL || ((MPI_Wtime() - t_last_logtime) > LOG_REFRESH_TIME_SEC))
        {
            char fileName[2 * MAX_HOST_SZ] = {0};

            if (log_fp != NULL)
                fclose(log_fp);

            char formatted_time[26] = {0};
            getformatted_time(formatted_time, 0);
            sprintf(fileName,"%s/tcp-%s-%d-%s.log", bench_options.logfolder, bench_options.uuid, world_rank, formatted_time);
            log_fp = fopen(fileName, "w");
	    t_last_logtime = MPI_Wtime();
        }

        MPI_Barrier(MPI_COMM_WORLD);

        t_start = MPI_Wtime();
        if (bench_options.use_dotnet)
        {
            // .Net based benchmark; MPI is used just for launching
            do_launch_dotnet_bench(my_group, world_rank, my_peer, peer_ipaddr, my_ipaddr,
                    buff_len, bench_options.iters, run_idx, bench_options.ppn);
        }
        else
        {
            // MPI based benchmark
            if (bench_options.uni_dir)
            {
                // uni-directional
                do_mpi_benchmark_unidir(my_group, world_rank, my_peer, my_peer_host, myhostname, 
                        bench_options.iters, buffer_tx, buffer_rx, buff_len);
            }
            else
            {
                // bi-directional
                if (bench_options.nonblocking)
                {
                    do_mpi_benchmark_nonblocking(my_group, world_rank, my_peer, my_peer_host, myhostname,
                            bench_options.iters, buffer_tx, buffer_rx, buff_len);
                }
                else
                {
                    do_mpi_benchmark(my_group, world_rank, my_peer, my_peer_host, myhostname,
                            bench_options.iters, buffer_tx, buffer_rx, buff_len);
                }
            }
        }
        t_end_local = MPI_Wtime();
        my_time = t_end_local - t_start;

#ifdef REPORT_BANDWIDTH
        if (my_group == 0)
        {
            long double gbits_transferred = 8.0 * buff_len * bench_options.iters * ((bench_options.uni_dir == 1) ? 1.0 : 2.0) * 1e-9;
            long double bandwidth = (gbits_transferred * 1.0) / my_time;
            fprintf(stderr, "[Rank: %d Run#: %lld]: Total Gbits: %Lf, Bandwidth: %.2Lf Gbps\n", world_rank, run_idx, gbits_transferred, bandwidth);
        }
#endif

	    // generate data for kusto ingestion; dotnet benchmark reports this inside the dotnet benchmark
        if (!bench_options.use_dotnet)
        {
            char formatted_time[MAX_HOST_SZ] = {0};
            getformatted_time(formatted_time, 1);

            // format: Timestamp:datetime,JobId:string,Rank:int,VMCount:int,LocalIP:string,RemoteIP:string,NumOfFlows:int,BufferSize:int,NumOfBuffers:int,TimeTakenms:real,RunId:int
            fprintf(log_fp, "%s,%s,%d,%d,%s,%s,%d,%d,%d,%.2lf,%lld\n", 
                    formatted_time, bench_options.uuid, world_rank, world_size/bench_options.ppn, 
                    my_ipaddr, peer_ipaddr, bench_options.ppn, buff_len, 
                    bench_options.iters, my_time * 1000.0, run_idx);
        }

        MPI_Barrier(MPI_COMM_WORLD);
        t_end = MPI_Wtime();

        MPI_Allreduce(&my_time, &min_time, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
        MPI_Allreduce(&my_time, &max_time, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
        MPI_Allreduce(&my_time, &sum_time, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

        if (world_rank == 0 && (run_idx % 1000 == 0))
        {
            fprintf(stderr, "[Run#: %lld]: Total time: %.2lf ms, Min: %.2lf ms, Max: %.2lf ms, Avg: %.2lf ms\n", run_idx,
			    (t_end - t_start) * 1000.0, min_time * 1000.0, max_time * 1000.0, (sum_time * 1000)/world_size);
        }
    }

    if (log_fp != NULL)
        fclose(log_fp);

    if (!bench_options.use_dotnet)
    {
        free(buffer_tx);
        free(buffer_rx);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Finalize();
}
