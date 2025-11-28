#ifndef QCHAT_TUI_HPP
#define QCHAT_TUI_HPP

#include <mutex>
#include <string>
#include <vector>

namespace qchat {

class Client;

class Tui {
public:
	Tui();

	void setClient(Client *client);

	// full-screen loop: sets up alternate screen, runs UI,
	// then restores original screen when exiting
	void runMainLoop();

	// called from Client recv thread
	void onServerLine(const std::string &line);

	static void handleSigInt(int sig);

private:
	Client *client_;
	bool running_;

	static inline std::atomic<bool> sigintReceived{false};

	std::mutex stateMutex_;
	std::vector<std::string> messages_;
	std::vector<std::string> pendingFromServer_;
	std::string inputLine_;
	int scrollOffset_; // 0 = bottom, >0 = scrolled up
	int termRows_;
	int termCols_;

	// terminal state
	bool termInit_;
	struct TermiosState {
		bool valid;
		void *data; // opaque pointer to store original termios
	};
	TermiosState termiosState_;

	void initTerminal();
	void restoreTerminal();
	void updateWindowSize();

	void drainServerMessages();
	void render();

	void handleKey(char ch);
	void handleEscape();
	void submitInput();
	void handleCommand(const std::string &cmdLine);
	void handleChat(const std::string &text);

	void scrollUp(int lines);
	void scrollDown(int lines);

	static std::string trimLocal(const std::string &s);
	static void clampScroll(int total,int page,int &scroll);

	// helpers
	void addLocalMessage(const std::string &msg);
};

} // namespace qchat

#endif
