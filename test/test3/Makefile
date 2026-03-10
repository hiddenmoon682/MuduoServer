CFLAG= -std=c++11
all : server client
server : server.cc 
	g++ -g $(CFLAG) $^ -o $@
client : client.cc
	g++ -g $(CFLAG) $^ -o $@
clean:
	rm -rf server client
