#include "tui.hpp"
#include "client.hpp"
#include "chat_common.hpp"

#include <iostream>
#include <cstdio>
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <csignal>

namespace qchat {

namespace {

static constexpr const char *CSI="\x1b[";
static constexpr const char *ESC_ALTSCREEN_ON="\x1b[?1049h";
static constexpr const char *ESC_ALTSCREEN_OFF="\x1b[?1049l";
static constexpr const char *ESC_CLEAR="\x1b[2J";
static constexpr const char *ESC_HOME="\x1b[H";
static constexpr const char *ESC_HIDE_CURSOR="\x1b[?25l";
static constexpr const char *ESC_SHOW_CURSOR="\x1b[?25h";
static constexpr const char *ESC_RESET="\x1b[0m";

static constexpr const char *FG_DEFAULT="\x1b[38;2;230;230;230m";
static constexpr const char *FG_SYS="\x1b[38;2;255;255;128m";
static constexpr const char *FG_ERR="\x1b[38;2;255;96;96m";
static constexpr const char *FG_OK="\x1b[38;2;144;238;144m";
static constexpr const char *FG_MSG="\x1b[38;2;128;200;255m";
static constexpr const char *FG_HIST="\x1b[38;2;255;192;255m";
static constexpr const char *FG_LOCAL="\x1b[38;2;255;180;128m";

static constexpr const char *BG_INPUT="\x1b[48;2;30;30;30m";
static constexpr const char *BG_MENU="\x1b[48;2;0;70;140m";

std::string colorizeMessage(const std::string &line) {
	if(line.size()>=4u && line.compare(0,4,"SYS ")==0){
		return std::string(FG_SYS)+line+ESC_RESET;
	}
	if(line.size()>=3u && line.compare(0,3,"OK ")==0){
		return std::string(FG_OK)+line+ESC_RESET;
	}
	if(line.size()>=3u && line.compare(0,3,"ERR")==0){
		return std::string(FG_ERR)+line+ESC_RESET;
	}
	if(line.size()>=4u && line.compare(0,4,"FROM")==0){
		return std::string(FG_MSG)+line+ESC_RESET;
	}
	if(line.size()>=7u && line.compare(0,7,"PRIVATE")==0){
		return std::string(FG_MSG)+line+ESC_RESET;
	}
	if(line.size()>=4u && line.compare(0,4,"HIST")==0){
		return std::string(FG_HIST)+line+ESC_RESET;
	}
	if(line.size()>=6u && line.compare(0,6,"LOCAL:")==0){
		return std::string(FG_LOCAL)+line+ESC_RESET;
	}
	return std::string(FG_DEFAULT)+line+ESC_RESET;
}

struct TermiosHolder {
	bool valid;
	struct termios orig;

	TermiosHolder():valid(false),orig{} {}
};

} // namespace

Tui::Tui()
	:client_(nullptr),
	running_(false),
	stateMutex_(),
	messages_(),
	pendingFromServer_(),
	inputLine_(),
	scrollOffset_(0),
	termRows_(24),
	termCols_(80),
	termInit_(false),
	termiosState_{} {
	termiosState_.valid=false;
	termiosState_.data=nullptr;
}

void Tui::setClient(Client *client) {
	client_=client;
}

void Tui::initTerminal() {
	if(termInit_)return;

	// save original termios
	auto *holder=new TermiosHolder();
	if(::tcgetattr(STDIN_FILENO,&holder->orig)==0){
		holder->valid=true;
	}else{
		holder->valid=false;
	}
	termiosState_.data=holder;
	termiosState_.valid=holder->valid;

	if(holder->valid){
		struct termios raw=holder->orig;
		raw.c_lflag&=static_cast<unsigned long>(~(ICANON|ECHO));
		raw.c_cc[VMIN]=0;
		raw.c_cc[VTIME]=1;
		if(::tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw)!=0){
			// ignore error, continue
		}
	}

	std::cout<<ESC_ALTSCREEN_ON<<ESC_CLEAR<<ESC_HOME<<ESC_HIDE_CURSOR<<std::flush;

	termInit_=true;
	updateWindowSize();
}

void Tui::restoreTerminal() {
	if(!termInit_)return;

	std::cout<<ESC_RESET<<ESC_SHOW_CURSOR<<ESC_ALTSCREEN_OFF<<std::flush;

	if(termiosState_.data!=nullptr){
		auto *holder=reinterpret_cast<TermiosHolder*>(termiosState_.data);
		if(holder->valid){
			::tcsetattr(STDIN_FILENO,TCSAFLUSH,&holder->orig);
		}
		delete holder;
		termiosState_.data=nullptr;
	}
	termiosState_.valid=false;
	termInit_=false;
}

void Tui::updateWindowSize() {
	struct winsize ws{};
	if(::ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws)==0){
		if(ws.ws_row>0)termRows_=static_cast<int>(ws.ws_row);
		if(ws.ws_col>0)termCols_=static_cast<int>(ws.ws_col);
	}
	if(termRows_<4)termRows_=4;
	if(termCols_<20)termCols_=20;
}

void Tui::onServerLine(const std::string &line) {
	std::lock_guard<std::mutex> lock(stateMutex_);
	pendingFromServer_.push_back(line);
}

void Tui::handleSigInt(int sig) {
	(void)sig;
	sigintReceived.store(true,std::memory_order_relaxed);
}

void Tui::drainServerMessages() {
	std::lock_guard<std::mutex> lock(stateMutex_);
	if(pendingFromServer_.empty())return;
	for(const std::string &s:pendingFromServer_){
		messages_.push_back(s);
	}
	pendingFromServer_.clear();
	if(scrollOffset_<0)scrollOffset_=0;
}

void Tui::addLocalMessage(const std::string &msg) {
	std::lock_guard<std::mutex> lock(stateMutex_);
	messages_.push_back("LOCAL: "+msg);
	if(scrollOffset_<0)scrollOffset_=0;
}

void Tui::clampScroll(int total,int page,int &scroll) {
	if(page<1)page=1;
	int maxScroll=0;
	if(total>page)maxScroll=total-page;
	if(scroll<0)scroll=0;
	if(scroll>maxScroll)scroll=maxScroll;
}

std::string Tui::trimLocal(const std::string &s) {
	return trim(s);
}

void Tui::render() {
	updateWindowSize();

	std::vector<std::string> msgs;
	std::string input;
	int scroll=0;

	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		msgs=messages_;
		input=inputLine_;
		scroll=scrollOffset_;
	}

	int total=static_cast<int>(msgs.size());
	int messageLines=termRows_-2;
	if(messageLines<1)messageLines=1;

	clampScroll(total,messageLines,scroll);

	int startIdx=0;
	if(total>messageLines){
		startIdx=total-messageLines-scroll;
		if(startIdx<0)startIdx=0;
	}

	std::cout<<ESC_HOME<<ESC_CLEAR;

	// message area
	for(int row=0;row<messageLines;++row){
		int idx=startIdx+row;
		std::string line;
		if(idx>=0 && idx<total){
			line=colorizeMessage(msgs[static_cast<std::size_t>(idx)]);
		}else{
			line="";
		}
		if(static_cast<int>(line.size())>termCols_){
			line=line.substr(0,static_cast<std::size_t>(termCols_));
		}
		std::cout<<line<<"\x1b[K\n";
	}

	// input line (second from bottom)
	std::string shown=input;
	if(static_cast<int>(shown.size())>termCols_-2){
		shown=shown.substr(static_cast<std::size_t>(shown.size()-(termCols_-2)));
	}
	int padInput=termCols_-2-static_cast<int>(shown.size());
	if(padInput<0)padInput=0;

	std::cout<<BG_INPUT<<FG_DEFAULT<<"> "<<shown;
	for(int i=0;i<padInput;++i)std::cout<<' ';
	std::cout<<ESC_RESET<<"\n";

	// menu bar (bottom line)
	std::string menu=" /signup /login /all /to /chpass /chhandle /chname /setmulti /history /logout /quit  ↑/↓ scroll";
	if(static_cast<int>(menu.size())>termCols_){
		menu=menu.substr(0,static_cast<std::size_t>(termCols_));
	}
	int padMenu=termCols_-static_cast<int>(menu.size());
	if(padMenu<0)padMenu=0;

	std::cout<<BG_MENU<<FG_DEFAULT<<menu;
	for(int i=0;i<padMenu;++i)std::cout<<' ';
	std::cout<<ESC_RESET<<std::flush;
}

void Tui::scrollUp(int lines) {
	if(lines<=0)return;
	std::lock_guard<std::mutex> lock(stateMutex_);
	int total=static_cast<int>(messages_.size());
	int messageLines=termRows_-2;
	if(messageLines<1)messageLines=1;
	clampScroll(total,messageLines,scrollOffset_);
	scrollOffset_+=lines;
	clampScroll(total,messageLines,scrollOffset_);
}

void Tui::scrollDown(int lines) {
	if(lines<=0)return;
	std::lock_guard<std::mutex> lock(stateMutex_);
	int total=static_cast<int>(messages_.size());
	int messageLines=termRows_-2;
	if(messageLines<1)messageLines=1;
	clampScroll(total,messageLines,scrollOffset_);
	scrollOffset_-=lines;
	clampScroll(total,messageLines,scrollOffset_);
}

void Tui::handleChat(const std::string &text) {
	if(client_==nullptr)return;
	if(text.empty())return;
	client_->sendLine("MSGALL "+text);
}

void Tui::handleCommand(const std::string &cmdLine) {
	std::string s=trimLocal(cmdLine);
	if(s.empty())return;

	// Extract command word (first token)
	std::size_t sp=s.find(' ');
	std::string cmd;
	if(sp==std::string::npos)cmd=s;
	else cmd=s.substr(0,sp);

	// normalize short commands by checking both cases
	if(cmd=="all" || cmd=="ALL"){
		// /all <message with spaces, unicode>
		if(sp==std::string::npos || sp+1>=s.size()){
			addLocalMessage("Usage: /all message");
			return;
		}
		std::string msg=s.substr(sp+1); // keep as-is
		if(client_!=nullptr)client_->sendLine("MSGALL "+msg);
		return;
	}

	if(cmd=="to" || cmd=="TO"){
		// /to <handle> <message with spaces, unicode>
		if(sp==std::string::npos){
			addLocalMessage("Usage: /to handle message");
			return;
		}
		std::size_t sp2=s.find(' ',sp+1);
		if(sp2==std::string::npos || sp2+1>=s.size()){
			addLocalMessage("Usage: /to handle message");
			return;
		}
		std::string handle=s.substr(sp+1,sp2-sp-1);
		std::string msg=s.substr(sp2+1);
		if(handle.empty()){
			addLocalMessage("Usage: /to handle message");
			return;
		}
		if(client_!=nullptr)client_->sendLine("MSGTO "+handle+" "+msg);
		return;
	}

	// For the rest, token-based parsing is fine
	auto toks=splitTokens(s,4);
	if(toks.empty())return;
	cmd=toks[0];

	if(cmd=="signup" || cmd=="SIGNUP"){
		if(toks.size()<4u){
			addLocalMessage("Usage: /signup handle password display_name");
			return;
		}
		if(client_!=nullptr){
			client_->sendLine("SIGNUP "+toks[1]+" "+toks[2]+" "+toks[3]);
		}
		return;
	}
	if(cmd=="login" || cmd=="LOGIN"){
		if(toks.size()<3u){
			addLocalMessage("Usage: /login handle password");
			return;
		}
		if(client_!=nullptr){
			client_->sendLine("LOGIN "+toks[1]+" "+toks[2]);
		}
		return;
	}
	if(cmd=="chpass" || cmd=="CHPASS"){
		if(toks.size()<3u){
			addLocalMessage("Usage: /chpass old new");
			return;
		}
		if(client_!=nullptr){
			client_->sendLine("CHPASS "+toks[1]+" "+toks[2]);
		}
		return;
	}
	if(cmd=="chhandle" || cmd=="CHHANDLE"){
		if(toks.size()<2u){
			addLocalMessage("Usage: /chhandle new_handle");
			return;
		}
		if(client_!=nullptr){
			client_->sendLine("CHHANDLE "+toks[1]);
		}
		return;
	}
	if(cmd=="chname" || cmd=="CHNAME"){
		if(toks.size()<2u){
			addLocalMessage("Usage: /chname display_name");
			return;
		}
		if(client_!=nullptr){
			// toks[1] is full remainder; may contain spaces and UTF-8
			client_->sendLine("CHNAME "+toks[1]);
		}
		return;
	}
	if(cmd=="setmulti" || cmd=="SETMULTI"){
		if(toks.size()<2u){
			addLocalMessage("Usage: /setmulti 0|1");
			return;
		}
		if(client_!=nullptr){
			client_->sendLine("SETMULTI "+toks[1]);
		}
		return;
	}
	if(cmd=="history" || cmd=="HISTORY"){
		if(client_!=nullptr){
			client_->sendLine("HISTORY");
		}
		return;
	}
	if(cmd=="logout" || cmd=="LOGOUT"){
		if(client_!=nullptr){
			client_->sendLine("LOGOUT");
		}
		return;
	}
	if(cmd=="quit" || cmd=="exit" || cmd=="QUIT" || cmd=="EXIT"){
		if(client_!=nullptr){
			client_->sendLine("QUIT");
		}
		running_=false;
		return;
	}
	if(cmd=="up"){
		scrollUp(1);
		return;
	}
	if(cmd=="down"){
		scrollDown(1);
		return;
	}
	if(cmd=="help"){
		addLocalMessage("Commands: /signup /login /all /to /chpass /chhandle /chname /setmulti /history /logout /quit");
		return;
	}

	addLocalMessage("Unknown command: "+cmd);
}

void Tui::submitInput() {
	std::string line;
	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		line=inputLine_;
		inputLine_.clear();
	}
	line=trimLocal(line);
	if(line.empty())return;
	if(line[0]=='/' || line[0]==':'){
		handleCommand(line.substr(1));
	}else{
		handleChat(line);
	}
}

void Tui::handleEscape() {
	char seq[2];
	ssize_t n=::read(STDIN_FILENO,seq,2);
	if(n<2)return;
	if(seq[0]=='['){
		if(seq[1]=='A'){
			scrollUp(1);
		}else if(seq[1]=='B'){
			scrollDown(1);
		}
	}
}

void Tui::handleKey(char ch) {
	constexpr bool allowUnicode=true;
	if(ch=='\r' || ch=='\n'){
		submitInput();
		return;
	}
	if(ch==3){
		// Ctrl-C
		running_=false;
		return;
	}
	if(ch==127 || ch=='\b'){
		std::lock_guard<std::mutex> lock(stateMutex_);
		if(!inputLine_.empty())inputLine_.pop_back();
		return;
	}
	if(ch=='\x1b'){
		handleEscape();
		return;
	}
	if constexpr(!allowUnicode){
		if(ch>=32 && static_cast<unsigned char>(ch)<127u){
			std::lock_guard<std::mutex> lock(stateMutex_);
			inputLine_.push_back(ch);
			return;
		}
	}else{
		std::lock_guard<std::mutex> lock(stateMutex_);
		inputLine_.push_back(ch);
		return;
	}
}

void Tui::runMainLoop() {
	struct sigaction sa{};
	sa.sa_handler=Tui::handleSigInt;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags=0;
	::sigaction(SIGINT,&sa,nullptr);

	initTerminal();
	running_=true;

	while(running_){
		if(sigintReceived.load(std::memory_order_relaxed)){
			addLocalMessage("SIGINT received. Exiting...");
			running_=false;
			break;
		}

		drainServerMessages();
		render();

		struct pollfd pfd{};
		pfd.fd=STDIN_FILENO;
		pfd.events=POLLIN;
		pfd.revents=0;
		int ret=::poll(&pfd,1,100);
		if(ret>0 && (pfd.revents&POLLIN)){
			char ch=0;
			ssize_t n=::read(STDIN_FILENO,&ch,1);
			if(n>0){
				handleKey(ch);
			}
		}
	}

	restoreTerminal();
}

} // namespace qchat
