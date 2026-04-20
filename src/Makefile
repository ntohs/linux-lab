#실제 파일이 아닌 '명령어 세트'임을 명시하는 설정
#파일명과 타겟명이 겹칠 때 발생하는 무시 현상을 방지함
.PHONY: all clean

include Makefile.inc

TARGET = linux_lab

OBJ = main.o debug.o worker.o input.o launcher.o shm.o dumpstate.o

all: $(OBJ)
	$(CXX) -o $(TARGET) $(OBJ) $(CXXLIBS) -lpthread
	mv $(TARGET) ..

debug.o: $(COMMON)/debug.h $(COMMON)/debug.c
	$(CC) -g $(CFLAGS) -c $(COMMON)/debug.c

launcher.o: $(CORE)/launcher.h $(CORE)/launcher.c
	$(CC) -g $(CFLAGS) -c $(CORE)/launcher.c

worker.o: $(CORE)/worker.h $(CORE)/worker.c
	$(CC) -g $(CFLAGS) -c $(CORE)/worker.c

shm.o: $(IPC)/shm.h $(IPC)/shm.c
	$(CC) -g $(CFLAGS) -c $(IPC)/shm.c

dumpstate.o: $(UTILS)/dumpstate.h $(UTILS)/dumpstate.c
	$(CC) -g $(CFLAGS) -c $(UTILS)/dumpstate.c

input.o: $(UTILS)/input.h $(UTILS)/input.c
	$(CC) -g $(CFLAGS) -c $(UTILS)/input.c

clean:
	rm -rf *.o ../linux_lab $(TMPDIR)
