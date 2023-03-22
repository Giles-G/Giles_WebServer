CXX = g++
CFLAGS = -std=c++14 -O2 -Wall -g
INC_CLUDE = -I../src

TARGET = test
OBJS = test.cc \
	   ../Log/log.cpp

run: $(OBJS)
	$(CXX) $(CFLAGS) $(OBJS) -o ./$(TARGET) $(INC_CLUDE) -lpthread 
