#include "tui.hpp"
#include "client.hpp"

#include <iostream>
#include <limits>

namespace qchat {

Tui::Tui():client_(nullptr) {}

void Tui::setClient(Client *client) {
	client_=client;
}

void Tui::printLine(const std::string &line) {
	std::lock_guard<std::mutex> lock(outMutex_);
	std::cout<<line<<'\n';
}

void Tui::onServerLine(const std::string &line) {
	printLine("[SERVER] "+line);
}

void Tui::showMenu() {
	printLine("==== qchat client ====");
	printLine("1) Sign up");
	printLine("2) Log in");
	printLine("3) Send broadcast message");
	printLine("4) Send private message");
	printLine("5) Change password");
	printLine("6) Change handle");
	printLine("7) Change display name");
	printLine("8) Set multi-login 0/1");
	printLine("9) Show login history");
	printLine("10) Logout");
	printLine("0) Quit");
}

int Tui::readMenuChoice() {
	printLine("Select: ");
	int choice=-1;
	for(;;){
		if(!(std::cin>>choice)){
			std::cin.clear();
			std::cin.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
			printLine("Invalid input, try again");
			continue;
		}
		std::cin.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
		return choice;
	}
}

std::string Tui::readLinePrompt(const std::string &prompt) {
	std::lock_guard<std::mutex> lock(outMutex_);
	std::cout<<prompt;
	std::cout.flush();
	std::string s;
	if(!std::getline(std::cin,s))return {};
	return s;
}

void Tui::runMainLoop() {
	if(client_==nullptr){
		printLine("Internal error: client not set");
		return;
	}
	bool running=true;
	while(running){
		showMenu();
		int choice=readMenuChoice();
		if(!std::cin.good()){
			break;
		}
		switch(choice){
		case 1:{
			std::string handle=readLinePrompt("Handle (ASCII, no spaces): ");
			std::string pw=readLinePrompt("Password: ");
			std::string name=readLinePrompt("Display name: ");
			std::string line="SIGNUP "+handle+" "+pw+" "+name;
			client_->sendLine(line);
			break;
		}
		case 2:{
			std::string handle=readLinePrompt("Handle: ");
			std::string pw=readLinePrompt("Password: ");
			std::string line="LOGIN "+handle+" "+pw;
			client_->sendLine(line);
			break;
		}
		case 3:{
			std::string msg=readLinePrompt("Message: ");
			std::string line="MSGALL "+msg;
			client_->sendLine(line);
			break;
		}
		case 4:{
			std::string to=readLinePrompt("Target handle: ");
			std::string msg=readLinePrompt("Message: ");
			std::string line="MSGTO "+to+" "+msg;
			client_->sendLine(line);
			break;
		}
		case 5:{
			std::string oldPw=readLinePrompt("Old password: ");
			std::string newPw=readLinePrompt("New password: ");
			std::string line="CHPASS "+oldPw+" "+newPw;
			client_->sendLine(line);
			break;
		}
		case 6:{
			std::string nh=readLinePrompt("New handle: ");
			std::string line="CHHANDLE "+nh;
			client_->sendLine(line);
			break;
		}
		case 7:{
			std::string nd=readLinePrompt("New display name: ");
			std::string line="CHNAME "+nd;
			client_->sendLine(line);
			break;
		}
		case 8:{
			std::string v=readLinePrompt("Allow multi-login (0/1): ");
			std::string line="SETMULTI "+v;
			client_->sendLine(line);
			break;
		}
		case 9:{
			client_->sendLine("HISTORY");
			break;
		}
		case 10:{
			client_->sendLine("LOGOUT");
			break;
		}
		case 0:{
			client_->sendLine("QUIT");
			running=false;
			break;
		}
		default:
			printLine("Unknown choice");
			break;
		}
	}
}

} // namespace qchat
