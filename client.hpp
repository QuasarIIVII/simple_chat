#ifndef QCHAT_CLIENT_HPP
#define QCHAT_CLIENT_HPP

#include <atomic>
#include <string>
#include <thread>
#include <mutex>

namespace qchat {

class Tui;

class Client {
public:
	Client(const std::string &host,unsigned short port,Tui &tui);
	~Client();

	bool connectToServer();
	void start();
	void stop();

	void sendLine(const std::string &line);

private:
	std::string host_;
	unsigned short port_;
	int sock_;
	std::atomic<bool> running_;
	std::jthread recvThread_;
	Tui &tui_;
	std::mutex sendMutex_;

	void recvLoop();
	static bool sendAll(int fd,const char *data,std::size_t len);
};

} // namespace qchat

#endif
