#ifndef QCHAT_TUI_HPP
#define QCHAT_TUI_HPP

#include <mutex>
#include <string>

namespace qchat {

class Client;

class Tui {
public:
	Tui();
	void setClient(Client *client);

	void runMainLoop();

	void onServerLine(const std::string &line);

private:
	Client *client_;
	std::mutex outMutex_;

	void printLine(const std::string &line);
	void showMenu();
	int readMenuChoice();
	std::string readLinePrompt(const std::string &prompt);
};

} // namespace qchat

#endif
