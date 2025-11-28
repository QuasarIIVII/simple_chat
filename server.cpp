#include "server.hpp"

#include <iostream>
#include <cstring>
#include <cerrno>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <netdb.h>

namespace qchat {

Server::Server(unsigned short port,const std::string &dbPath)
	:port_(port),
	dbPath_(dbPath),
	listenFd_(-1),
	running_(false),
	dbFile_(dbPath) {}

Server::~Server() {
	if(listenFd_>=0){
		::close(listenFd_);
	}
	for(auto &kv:clients_){
		if(kv.second.fd>=0)::close(kv.second.fd);
	}
}

bool Server::init() {
	if(!dbFile_.load(db_)){
		std::cerr<<"Failed to load DB from "<<dbPath_<<"\n";
		return false;
	}
	if(!setupListenSocket())return false;
	running_=true;
	return true;
}

bool Server::setupListenSocket() {
	listenFd_=::socket(AF_INET,SOCK_STREAM,0);
	if(listenFd_<0){
		std::perror("socket");
		return false;
	}
	int one=1;
	if(::setsockopt(listenFd_,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one))<0){
		std::perror("setsockopt");
		::close(listenFd_);
		listenFd_=-1;
		return false;
	}
	sockaddr_in addr{};
	addr.sin_family=AF_INET;
	addr.sin_addr.s_addr=htonl(INADDR_ANY);
	addr.sin_port=htons(port_);
	if(::bind(listenFd_,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))<0){
		std::perror("bind");
		::close(listenFd_);
		listenFd_=-1;
		return false;
	}
	if(::listen(listenFd_,16)<0){
		std::perror("listen");
		::close(listenFd_);
		listenFd_=-1;
		return false;
	}
	std::cout<<"Server listening on port "<<port_<<"\n";
	return true;
}

void Server::run() {
	if(!running_)return;
	mainLoop();
}

void Server::mainLoop() {
	while(running_){
		std::vector<pollfd> fds;
		fds.reserve(1+clients_.size());
		pollfd pfd{};
		pfd.fd=listenFd_;
		pfd.events=POLLIN;
		pfd.revents=0;
		fds.push_back(pfd);
		for(auto &kv:clients_){
			pollfd cfd{};
			cfd.fd=kv.first;
			cfd.events=POLLIN;
			cfd.revents=0;
			fds.push_back(cfd);
		}
		int ret=::poll(fds.data(),static_cast<nfds_t>(fds.size()),1000);
		if(ret<0){
			if(errno==EINTR)continue;
			std::perror("poll");
			break;
		}
		if(ret==0)continue;
		if(fds[0].revents&POLLIN){
			handleNewConnection();
		}
		for(std::size_t i=1;i<fds.size();++i){
			if(fds[i].revents&POLLIN){
				handleClientReadable(fds[i].fd);
			}else if(fds[i].revents&(POLLHUP|POLLERR|POLLNVAL)){
				closeClient(fds[i].fd);
			}
		}
	}
}

void Server::handleNewConnection() {
	sockaddr_in addr{};
	socklen_t alen=sizeof(addr);
	int fd=::accept(listenFd_,reinterpret_cast<sockaddr*>(&addr),&alen);
	if(fd<0){
		std::perror("accept");
		return;
	}
	char ipbuf[INET_ADDRSTRLEN];
	const char *ptr=::inet_ntop(AF_INET,&addr.sin_addr,ipbuf,sizeof(ipbuf));
	std::string ip;
	if(ptr!=nullptr)ip=ptr;
	else ip="unknown";
	ClientConn c;
	c.fd=fd;
	c.loggedIn=false;
	c.uid=0;
	c.handle.clear();
	c.peerIp=ip;
	clients_.emplace(fd,std::move(c));
	sendLine(fd,"SYS Welcome to qchat server\n");
}

void Server::handleClientReadable(int fd) {
	auto it=clients_.find(fd);
	if(it==clients_.end())return;
	ClientConn &c=it->second;
	char buf[1024];
	ssize_t n=::recv(fd,buf,sizeof(buf),0);
	if(n<=0){
		closeClient(fd);
		return;
	}
	c.recvBuf.append(buf,buf+static_cast<std::size_t>(n));
	for(;;){
		std::size_t pos=c.recvBuf.find('\n');
		if(pos==std::string::npos)break;
		std::string line=c.recvBuf.substr(0,pos);
		c.recvBuf.erase(0,pos+1);
		line=trim(line);
		if(line.empty())continue;
		processLine(c,line);
	}
}

void Server::closeClient(int fd) {
	auto it=clients_.find(fd);
	if(it==clients_.end())return;
	::close(fd);
	clients_.erase(it);
}

void Server::broadcast(const std::string &msg,int exceptFd) {
	for(auto &kv:clients_){
		if(kv.first==exceptFd)continue;
		sendLine(kv.first,msg);
	}
}

bool Server::sendAll(int fd,const char *data,std::size_t len) {
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

void Server::sendLine(int fd,const std::string &line) {
	std::string out=line;
	if(out.empty() || out.back()!='\n')out.push_back('\n');
	sendAll(fd,out.data(),out.size());
}

User *Server::findUserByHandle(const std::string &handle) {
	auto it=db_.uidByHandle.find(handle);
	if(it==db_.uidByHandle.end())return nullptr;
	return findUserById(it->second);
}

User *Server::findUserById(u64 uid) {
	auto it=db_.usersById.find(uid);
	if(it==db_.usersById.end())return nullptr;
	return &it->second;
}

void Server::recordLogin(User &u,const std::string &ip) {
	LoginRecord rec;
	rec.epochSeconds=nowEpochSeconds();
	rec.ip=ip;
	u.history.push_back(rec);
	const std::size_t maxHist=32;
	if(u.history.size()>maxHist){
		const std::size_t drop=u.history.size()-maxHist;
		u.history.erase(u.history.begin(),u.history.begin()+static_cast<std::ptrdiff_t>(drop));
	}
}

void Server::processLine(ClientConn &c,const std::string &line) {
	std::string trimmed=trim(line);
	if(trimmed.empty())return;

	std::size_t sp=trimmed.find(' ');
	std::string cmd;
	std::string rest;

	if(sp==std::string::npos){
		cmd=trimmed;
		rest.clear();
	}else{
		cmd=trimmed.substr(0,sp);
		rest=trim(trimmed.substr(sp+1));
	}

	if(cmd=="SIGNUP"){
		cmdSignup(c,rest);
	}else if(cmd=="LOGIN"){
		cmdLogin(c,rest);
	}else if(cmd=="MSGALL"){
		cmdMsgAll(c,rest);
	}else if(cmd=="MSGTO"){
		cmdMsgTo(c,rest);
	}else if(cmd=="CHPASS"){
		cmdChPass(c,rest);
	}else if(cmd=="CHHANDLE"){
		cmdChHandle(c,rest);
	}else if(cmd=="CHNAME"){
		cmdChName(c,rest);
	}else if(cmd=="SETMULTI"){
		cmdSetMulti(c,rest);
	}else if(cmd=="HISTORY"){
		cmdHistory(c);
	}else if(cmd=="LOGOUT"){
		cmdLogout(c);
	}else if(cmd=="QUIT"){
		closeClient(c.fd);
	}else{
		sendLine(c.fd,"ERR Unknown command");
	}
}

void Server::cmdSignup(ClientConn &c,const std::string &rest) {
	if(c.loggedIn){
		sendLine(c.fd,"ERR Already logged in");
		return;
	}
	// rest = "handle password display_name(with spaces, unicode...)"
	auto toks=splitTokens(rest,3); // [handle][password][display name...]
	if(toks.size()<3u){
		sendLine(c.fd,"ERR Usage: SIGNUP handle password display_name");
		return;
	}
	const std::string &handle=toks[0];
	const std::string &pw=toks[1];
	const std::string &display=toks[2];

	if(!isValidHandle(handle)){
		sendLine(c.fd,"ERR Invalid handle");
		return;
	}
	if(db_.uidByHandle.find(handle)!=db_.uidByHandle.end()){
		sendLine(c.fd,"ERR Handle already exists");
		return;
	}

	User u{};
	u.uid=db_.nextUid++;
	u.handle=handle;
	u.displayName=display; // spaces & UTF-8 allowed
	u.passwordHash=hashPassword(pw);
	u.allowMultiLogin=false;

	db_.uidByHandle[handle]=u.uid;
	db_.usersById.emplace(u.uid,std::move(u));
	saveDbIfPossible();
	sendLine(c.fd,"OK Signup successful");
}

void Server::cmdLogin(ClientConn &c,const std::string &rest) {
	// rest = "handle password"
	auto toks=splitTokens(rest,3);
	if(toks.size()<2u){
		sendLine(c.fd,"ERR Usage: LOGIN handle password");
		return;
	}
	const std::string &handle=toks[0];
	const std::string &pw=toks[1];

	User *u=findUserByHandle(handle);
	if(u==nullptr){
		sendLine(c.fd,"ERR No such user");
		return;
	}
	if(!passwordMatches(u->passwordHash,pw)){
		sendLine(c.fd,"ERR Invalid password");
		return;
	}
	if(!u->allowMultiLogin){
		for(auto &kv:clients_){
			if(kv.second.loggedIn && kv.second.uid==u->uid && kv.second.fd!=c.fd){
				sendLine(c.fd,"ERR Multiple logins disabled for this account");
				return;
			}
		}
	}
	c.loggedIn=true;
	c.uid=u->uid;
	c.handle=u->handle;

	recordLogin(*u,c.peerIp);
	saveDbIfPossible();

	std::string ok="OK Login successful as "+u->displayName+" (@"+u->handle+")";
	sendLine(c.fd,ok);

	std::string sys="SYS "+u->displayName+" (@"+u->handle+") joined chat";
	broadcast(sys,c.fd);
}

void Server::cmdMsgAll(ClientConn &c,const std::string &rest) {
	if(!c.loggedIn){
		sendLine(c.fd,"ERR Not logged in");
		return;
	}
	if(rest.empty()){
		sendLine(c.fd,"ERR Usage: MSGALL message");
		return;
	}

	User *u=findUserById(c.uid);
	if(u==nullptr){
		sendLine(c.fd,"ERR Internal error");
		return;
	}

	const std::string &text=rest; // full message with spaces & UTF-8
	std::string line="FROM "+u->displayName+" (@"+u->handle+"): "+text;
	broadcast(line,-1);
}

void Server::cmdMsgTo(ClientConn &c,const std::string &rest) {
	if(!c.loggedIn){
		sendLine(c.fd,"ERR Not logged in");
		return;
	}
	// rest = "handle message..."
	auto toks=splitTokens(rest,2); // [handle][message...]
	if(toks.size()<2u){
		sendLine(c.fd,"ERR Usage: MSGTO handle message");
		return;
	}
	const std::string &dstHandle=toks[0];
	const std::string &text=toks[1];

	User *dst=findUserByHandle(dstHandle);
	if(dst==nullptr){
		sendLine(c.fd,"ERR No such user");
		return;
	}
	User *src=findUserById(c.uid);
	if(src==nullptr){
		sendLine(c.fd,"ERR Internal error");
		return;
	}

	bool sent=false;
	for(auto &kv:clients_){
		ClientConn &other=kv.second;
		if(other.loggedIn && other.uid==dst->uid){
			std::string line="PRIVATE from "+src->displayName+" (@"+src->handle+"): "+text;
			sendLine(other.fd,line);
			sent=true;
		}
	}
	if(!sent){
		sendLine(c.fd,"ERR Target user not online");
	}else{
		sendLine(c.fd,"OK Private message sent");
	}
}

void Server::cmdChPass(ClientConn &c,const std::string &rest) {
	if(!c.loggedIn){
		sendLine(c.fd,"ERR Not logged in");
		return;
	}
	auto toks=splitTokens(rest,3); // old, new
	if(toks.size()<2u){
		sendLine(c.fd,"ERR Usage: CHPASS old new");
		return;
	}
	const std::string &oldPw=toks[0];
	const std::string &newPw=toks[1];

	User *u=findUserById(c.uid);
	if(u==nullptr){
		sendLine(c.fd,"ERR Internal error");
		return;
	}
	if(!passwordMatches(u->passwordHash,oldPw)){
		sendLine(c.fd,"ERR Old password mismatch");
		return;
	}
	u->passwordHash=hashPassword(newPw);
	saveDbIfPossible();
	sendLine(c.fd,"OK Password changed");
}

void Server::cmdChHandle(ClientConn &c,const std::string &rest) {
	if(!c.loggedIn){
		sendLine(c.fd,"ERR Not logged in");
		return;
	}
	auto toks=splitTokens(rest,2); // new_handle
	if(toks.empty()){
		sendLine(c.fd,"ERR Usage: CHHANDLE new_handle");
		return;
	}
	const std::string &newHandle=toks[0];

	if(!isValidHandle(newHandle)){
		sendLine(c.fd,"ERR Invalid handle");
		return;
	}
	if(db_.uidByHandle.find(newHandle)!=db_.uidByHandle.end()){
		sendLine(c.fd,"ERR Handle already exists");
		return;
	}
	User *u=findUserById(c.uid);
	if(u==nullptr){
		sendLine(c.fd,"ERR Internal error");
		return;
	}
	db_.uidByHandle.erase(u->handle);
	u->handle=newHandle;
	db_.uidByHandle[newHandle]=u->uid;
	c.handle=newHandle;
	saveDbIfPossible();
	sendLine(c.fd,"OK Handle changed");
}

void Server::cmdChName(ClientConn &c,const std::string &rest) {
	if(!c.loggedIn){
		sendLine(c.fd,"ERR Not logged in");
		return;
	}
	if(rest.empty()){
		sendLine(c.fd,"ERR Usage: CHNAME display_name");
		return;
	}
	User *u=findUserById(c.uid);
	if(u==nullptr){
		sendLine(c.fd,"ERR Internal error");
		return;
	}
	u->displayName=rest; // full string, spaces, UTF-8 allowed
	saveDbIfPossible();
	sendLine(c.fd,"OK Display name changed");
}

void Server::cmdSetMulti(ClientConn &c,const std::string &rest) {
	if(!c.loggedIn){
		sendLine(c.fd,"ERR Not logged in");
		return;
	}
	auto toks=splitTokens(rest,2);
	if(toks.empty()){
		sendLine(c.fd,"ERR Usage: SETMULTI 0|1");
		return;
	}
	const std::string &v=toks[0];

	User *u=findUserById(c.uid);
	if(u==nullptr){
		sendLine(c.fd,"ERR Internal error");
		return;
	}
	if(v=="0"){
		u->allowMultiLogin=false;
	}else if(v=="1"){
		u->allowMultiLogin=true;
	}else{
		sendLine(c.fd,"ERR Value must be 0 or 1");
		return;
	}
	saveDbIfPossible();
	sendLine(c.fd,"OK Multi-login setting updated");
}

void Server::cmdHistory(ClientConn &c) {
	if(!c.loggedIn){
		sendLine(c.fd,"ERR Not logged in");
		return;
	}
	User *u=findUserById(c.uid);
	if(u==nullptr){
		sendLine(c.fd,"ERR Internal error");
		return;
	}
	std::ostringstream oss;
	oss<<"HIST "<<u->history.size();
	sendLine(c.fd,oss.str());
	for(const LoginRecord &rec:u->history){
		std::ostringstream ln;
		ln<<"HIST "<<rec.epochSeconds<<" "<<rec.ip;
		sendLine(c.fd,ln.str());
	}
}

void Server::cmdLogout(ClientConn &c) {
	if(!c.loggedIn){
		sendLine(c.fd,"ERR Not logged in");
		return;
	}
	c.loggedIn=false;
	c.uid=0;
	c.handle.clear();
	sendLine(c.fd,"OK Logged out");
}

void Server::saveDbIfPossible() {
	if(!dbFile_.save(db_)){
		std::cerr<<"Warning: failed to save DB\n";
	}
}

} // namespace qchat
