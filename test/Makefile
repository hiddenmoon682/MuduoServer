CFLAG= -std=c++11
all : test_s test_c
test_s : test_server.cc ../source/Server.hpp
	g++ -g $(CFLAG) $^ -o $@
test_c : test_client.cc ../source/Server.hpp
	g++ -g $(CFLAG) $^ -o $@
clean:
	rm -rf test_s test_c
