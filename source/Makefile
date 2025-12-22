CFLAG= -std=c++11
all : test
test : main.cc Server.hpp
	g++  -g $(CFLAG) $^ -o $@
clean:
	rm -rf test
