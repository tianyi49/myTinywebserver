CXX ?= g++
DEBUG ?= 0
CXXFLAGS=-std=c++17
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif
OBJS =  main.cpp  ./timer/heap_timer.cpp ./http/http_conn.cpp \
./log/log.cpp ./CGImysql/sql_connection_pool.cpp  ./webserver/webserver.cpp config.cpp

server:
	$(CXX) $(OBJS) -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient

clean:
	rm  -r server
