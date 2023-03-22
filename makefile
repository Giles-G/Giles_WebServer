CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

server: main.cpp  ./Timer/timer.cpp ./HttpConn/http_conn.cpp ./Log/log.cpp ./ConnPool/sql_connection_pool.cpp  ./Server/webserver.cpp config.cpp
	$(CXX) -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient

clean:
	rm  -r server
