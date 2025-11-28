#ifndef QCHAT_SERVER_HPP
#define QCHAT_SERVER_HPP

#include "chat_common.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace qchat {

struct ClientConn {
	int fd{-1};
	std::string recvBuf;
	bool loggedIn{false};
	u64 uid{0};
	std::string handle;
	std::string peerIp;
};

class Server {
public:
	Server(unsigned short port,const std::string &dbPath);
	~Server();

	bool init();
	void run();

private:
	unsigned short port_;
	std::string dbPath_;
	int listenFd_;
	bool running_;

	DbState db_;
	DbFile dbFile_;

	std::unordered_map<int,ClientConn> clients_;

	bool setupListenSocket();
	void mainLoop();
	void handleNewConnection();
	void handleClientReadable(int fd);
	void closeClient(int fd);

	void broadcast(const std::string &msg,int exceptFd);
	void sendLine(int fd,const std::string &line);
	static bool sendAll(int fd,const char *data,std::size_t len);

	void processLine(ClientConn &c,const std::string &line);
	void cmdSignup(ClientConn &c,const std::vector<std::string> &toks);
	void cmdLogin(ClientConn &c,const std::vector<std::string> &toks);
	void cmdMsgAll(ClientConn &c,const std::vector<std::string> &toks);
	void cmdMsgTo(ClientConn &c,const std::vector<std::string> &toks);
	void cmdChPass(ClientConn &c,const std::vector<std::string> &toks);
	void cmdChHandle(ClientConn &c,const std::vector<std::string> &toks);
	void cmdChName(ClientConn &c,const std::vector<std::string> &toks);
	void cmdSetMulti(ClientConn &c,const std::vector<std::string> &toks);
	void cmdHistory(ClientConn &c);
	void cmdLogout(ClientConn &c);

	User *findUserByHandle(const std::string &handle);
	User *findUserById(u64 uid);
	void recordLogin(User &u,const std::string &ip);

	void saveDbIfPossible();
};

} // namespace qchat

#endif
