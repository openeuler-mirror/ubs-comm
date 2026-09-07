CC = g++
CFLAGS = -std=c++11 -Wall -Wextra -O2 -pthread

TARGET = ub_bench_epoll
SRC = ub_bench_epoll.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean
