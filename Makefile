ld=link
cc=cl

cflags=/I"C:\Program Files (x86)\Microsoft SDKs\MPI\Include"
ldflags=/libpath:"C:\Program Files (x86)\Microsoft SDKs\MPI\Lib\x64"

libs=msmpi.lib

output=mpi_perf.exe
objs=mpi_perf.obj

# output=get_adder_info.exe
# objs=get_adder_info.obj

all: $(objs)
	$(ld) $(libs) $(ldflags) -out:$(output) $(objs)

.c.obj:
	$(cc) $(cflags) /c $*.c

clean:
	@del /Q $(objs)
	@del /Q $(output)
