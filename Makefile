CC = g++ -g3
CFLAGS = -g3
TARGET1 = worker
TARGET2 = oss

OBJS1 = worker.o
OBJS2 = oss.o

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(OBJS1)
        $(CC) -o $(TARGET1) $(OBJS1)

$(TARGET2): $(OBJS2)
        $(CC) -o $(TARGET2) $(OBJS2)

worker.o: worker.cpp shm.h msgq.h
        $(CC) $(CFLAGS) -c worker.cpp

oss.o: oss.cpp shm.h msgq.h
        $(CC) $(CFLAGS) -c oss.cpp

clean:
        /bin/rm -f *.o $(TARGET1) $(TARGET2) msgq.txt logfile
