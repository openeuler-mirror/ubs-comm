CC = g++
CFLAGS = -std=c++11 -Wall -Wextra -O2 -pthread
INCLUDES = -I./common

# 目标 - 修改为不同的名称以避免与目录冲突
TARGETS = server_test client_test

# 源文件
COMMON_SRCS = common/test_common.cpp
SERVER_SRCS = server/tcp_server_all.cpp server/test_handlers.cpp
CLIENT_SRCS = client/tcp_client_all.cpp client/test_cases.cpp

# 目标文件
COMMON_OBJS = $(COMMON_SRCS:.cpp=.o)
SERVER_OBJS = $(SERVER_SRCS:.cpp=.o) $(COMMON_OBJS)
CLIENT_OBJS = $(CLIENT_SRCS:.cpp=.o) $(COMMON_OBJS)

# 默认目标
all: $(TARGETS)

# 服务器可执行文件
server_test: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(INCLUDES)

# 客户端可执行文件
client_test: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(INCLUDES)

# 编译规则
%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@ $(INCLUDES)

# 清理
clean:
	rm -f $(SERVER_OBJS) $(CLIENT_OBJS) $(COMMON_OBJS) tcp_full_server tcp_full_client

# 安装
install: all
	cp tcp_full_server tcp_full_client /usr/local/bin/

# 生成测试数据
data:
	@echo "Generating test data..."
	@mkdir -p data
	@head -c 1024 /dev/urandom > data/test_small.txt
	@head -c 1048576 /dev/urandom > data/test_large.txt
	@head -c 10485760 /dev/urandom > data/test_binary.bin
	@echo "Test data generated in data/ directory"

.PHONY: all clean install data
