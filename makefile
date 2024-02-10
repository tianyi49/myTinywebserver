CXX ?= g++
DEBUG ?= 0
CXXFLAGS=-std=c++14
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif
OBJS = ./timer/heap_timer.cpp ./http/http_conn.cpp ./buffer/buffer.cpp\
./log/log.cpp ./CGImysql/sql_connection_pool.cpp  ./webserver/webserver.cpp config.cpp

server: main.cpp $(OBJS)
	$(CXX)  -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient

clean:
	rm  -r server
