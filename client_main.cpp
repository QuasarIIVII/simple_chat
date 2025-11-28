#include "tui.hpp"
#include "client.hpp"

#include <iostream>

int main(int argc,char **argv) {
	std::string host="127.0.0.1";
	unsigned short port=5555;
	if(argc>=2){
		host=argv[1];
	}
	if(argc>=3){
		port=static_cast<unsigned short>(std::stoi(argv[2]));
	}
	qchat::Tui tui;
	qchat::Client client(host,port,tui);
	tui.setClient(&client);
	client.start();
	tui.runMainLoop();
	client.stop();
	return 0;
}
