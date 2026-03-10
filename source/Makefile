CFLAG= -std=c++11 -pthread
all : test
test : main.cc Server.hpp
	g++  -g $(CFLAG) main.cc -o $@
clean:
	rm -rf test
