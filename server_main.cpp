#include "server.hpp"

#include <iostream>

int main(int argc,char **argv) {
	unsigned short port=5555;
	std::string dbPath="qchat.db";
	if(argc>=2){
		port=static_cast<unsigned short>(std::stoi(argv[1]));
	}
	if(argc>=3){
		dbPath=argv[2];
	}
	qchat::Server srv(port,dbPath);
	if(!srv.init()){
		std::cerr<<"Failed to initialize server\n";
		return 1;
	}
	srv.run();
	return 0;
}
