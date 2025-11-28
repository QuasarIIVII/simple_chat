#include "client.hpp"
#include "tui.hpp"

#include <iostream>
#include <cstring>
#include <cerrno>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

namespace qchat {

Client::Client(const std::string &host,unsigned short port,Tui &tui)
	:host_(host),
	port_(port),
	sock_(-1),
	running_(false),
	tui_(tui) {}

Client::~Client() {
	stop();
}

bool Client::connectToServer() {
	struct addrinfo hints{};
	std::memset(&hints,0,sizeof(hints));
	hints.ai_family=AF_UNSPEC;
	hints.ai_socktype=SOCK_STREAM;
	hints.ai_protocol=IPPROTO_TCP;
	std::string portStr=std::to_string(port_);
	struct addrinfo *res=nullptr;
	int err=::getaddrinfo(host_.c_str(),portStr.c_str(),&hints,&res);
	if(err!=0){
		std::cerr<<"getaddrinfo: "<<::gai_strerror(err)<<"\n";
		return false;
	}
	int fd=-1;
	for(struct addrinfo *p=res;p!=nullptr;p=p->ai_next){
		fd=::socket(p->ai_family,p->ai_socktype,p->ai_protocol);
		if(fd<0)continue;
		if(::connect(fd,p->ai_addr,p->ai_addrlen)==0){
			break;
		}
		::close(fd);
		fd=-1;
	}
	::freeaddrinfo(res);
	if(fd<0){
		std::cerr<<"Failed to connect to "<<host_<<":"<<port_<<"\n";
		return false;
	}
	sock_=fd;
	return true;
}

void Client::start() {
	if(sock_<0){
		if(!connectToServer())return;
	}
	running_=true;
	recvThread_=std::jthread([this](){recvLoop();});
}

void Client::stop() {
	running_=false;
	if(sock_>=0){
		::shutdown(sock_,SHUT_RDWR);
		::close(sock_);
		sock_=-1;
	}
	if(recvThread_.joinable()){
		recvThread_.join();
	}
}

bool Client::sendAll(int fd,const char *data,std::size_t len) {
	std::size_t off=0;
	while(off<len){
		ssize_t n=::send(fd,data+off,len-off,0);
		if(n<0){
			if(errno==EINTR)continue;
			return false;
		}
		if(n==0)return false;
		off+=static_cast<std::size_t>(n);
	}
	return true;
}

void Client::sendLine(const std::string &line) {
	if(sock_<0)return;
	std::string out=line;
	if(out.empty() || out.back()!='\n')out.push_back('\n');
	std::lock_guard<std::mutex> lock(sendMutex_);
	if(!sendAll(sock_,out.data(),out.size())){
		tui_.onServerLine("Error sending data, disconnecting");
		stop();
	}
}

void Client::recvLoop() {
	std::string buf;
	buf.reserve(1024);
	char tmp[1024];
	while(running_){
		ssize_t n=::recv(sock_,tmp,sizeof(tmp),0);
		if(n<0){
			if(errno==EINTR)continue;
			tui_.onServerLine("Receive error, closing");
			break;
		}
		if(n==0){
			tui_.onServerLine("Server closed connection");
			break;
		}
		buf.append(tmp,tmp+static_cast<std::size_t>(n));
		for(;;){
			std::size_t pos=buf.find('\n');
			if(pos==std::string::npos)break;
			std::string line=buf.substr(0,pos);
			buf.erase(0,pos+1);
			tui_.onServerLine(line);
		}
	}
	running_=false;
}

} // namespace qchat
