CXX = g++
CFLAGS = -std=c++14 -O2 -Wall -g

TARGET = threadpoolTest
OBJS = threadpoolTest.cpp \
	   ccthreadPool.cpp

run: $(OBJS)
	$(CXX) $(CFLAGS) $(OBJS) -o ./$(TARGET) -pthread

clean:
	rm  -r $(TARGET)