SCRIPTS_DIR := $(HOME)/.config/mpv/scripts
CXXFLAGS := -Wall -Wextra -O2

party.so: party.cpp config.h
	g++ party.cpp -o party.so $(CXXFLAGS) -shared -fPIC

server: server.cpp config.h
	g++ server.cpp $(CXXFLAGS) -o server

.PHONY: install clean

install: party.so
	install -Dt $(SCRIPTS_DIR) party.so

clean:
	rm party.so server
