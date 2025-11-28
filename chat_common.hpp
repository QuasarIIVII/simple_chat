#ifndef QCHAT_COMMON_HPP
#define QCHAT_COMMON_HPP

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <fstream>
#include <sstream>
#include <algorithm>

#include "qhash.hpp"

namespace qchat {

using u8=qhash::u8;
using u64=qhash::u64;

struct LoginRecord {
	u64 epochSeconds{};
	std::string ip;
};

struct User {
	u64 uid{};
	std::string handle;       // unique, ASCII, no whitespace
	std::string displayName;  // UTF-8, arbitrary
	std::array<u8,64> passwordHash{};
	bool allowMultiLogin{false};
	std::vector<LoginRecord> history;
};

struct DbState {
	u64 nextUid{1};
	std::unordered_map<u64,User> usersById;
	std::unordered_map<std::string,u64> uidByHandle;
};

inline u64 nowEpochSeconds() {
	using namespace std::chrono;
	return static_cast<u64>(duration_cast<seconds>(
		system_clock::now().time_since_epoch()
	).count());
}

inline std::array<u8,64> hashPassword(const std::string &pw) {
	return qhash::sha3_512_bytes(pw.data(),pw.size());
}

inline bool passwordMatches(const std::array<u8,64> &hash,const std::string &pw) {
	auto h=hashPassword(pw);
	return std::equal(h.begin(),h.end(),hash.begin());
}

inline bool isValidHandle(const std::string &h) {
	if(h.empty())return false;
	for(char c:h){
		unsigned char uc=static_cast<unsigned char>(c);
		if(uc>127u)return false;
		if(uc<=32u)return false; // control & whitespace (includes space)
		if(uc==127u)return false;
	}
	return true;
}

inline std::string trim(const std::string &s) {
	std::size_t b=0;
	while(b<s.size() && static_cast<unsigned char>(s[b])<=32u)++b;
	std::size_t e=s.size();
	while(e>b && static_cast<unsigned char>(s[e-1])<=32u)--e;
	return s.substr(b,e-b);
}

inline std::vector<std::string> splitTokens(const std::string &line,std::size_t maxTokens) {
	std::vector<std::string> out;
	out.reserve(maxTokens);
	std::size_t i=0;
	while(i<line.size() && out.size()+1<maxTokens){
		while(i<line.size() && static_cast<unsigned char>(line[i])<=32u)++i;
		if(i>=line.size())break;
		std::size_t j=i;
		while(j<line.size() && static_cast<unsigned char>(line[j])>32u)++j;
		out.emplace_back(line.substr(i,j-i));
		i=j;
	}
	while(i<line.size() && static_cast<unsigned char>(line[i])<=32u)++i;
	if(i<line.size()){
		out.emplace_back(line.substr(i));
	}
	return out;
}

// Binary persistence (simple, local-endian, not portable across architectures)
class DbFile {
public:
	explicit DbFile(const std::string &path):path_(path) {}

	bool load(DbState &state) {
		std::ifstream in(path_,std::ios::binary);
		if(!in.good())return true; // treat as empty DB
		char magic[8];
		in.read(magic,8);
		if(!in.good())return false;
		const char expected[8]={'Q','C','H','A','T','D','B','1'};
		for(std::size_t i=0;i<8;++i){
			if(magic[i]!=expected[i])return false;
		}
		u64 nextUid=0;
		in.read(reinterpret_cast<char*>(&nextUid),sizeof(nextUid));
		if(!in.good())return false;
		state.nextUid=nextUid;

		u64 userCount=0;
		in.read(reinterpret_cast<char*>(&userCount),sizeof(userCount));
		if(!in.good())return false;

		state.usersById.clear();
		state.uidByHandle.clear();

		for(u64 idx=0;idx<userCount;++idx){
			User u{};
			if(!readString(in,u.handle))return false;
			if(!readString(in,u.displayName))return false;
			u64 uid=0;
			in.read(reinterpret_cast<char*>(&uid),sizeof(uid));
			if(!in.good())return false;
			u.uid=uid;
			u8 multi=0;
			in.read(reinterpret_cast<char*>(&multi),sizeof(multi));
			if(!in.good())return false;
			u.allowMultiLogin=(multi!=0u);
			in.read(reinterpret_cast<char*>(u.passwordHash.data()),u.passwordHash.size());
			if(!in.good())return false;

			u64 histCount=0;
			in.read(reinterpret_cast<char*>(&histCount),sizeof(histCount));
			if(!in.good())return false;
			u.history.clear();
			u.history.reserve(static_cast<std::size_t>(histCount));
			for(u64 hi=0;hi<histCount;++hi){
				LoginRecord rec{};
				in.read(reinterpret_cast<char*>(&rec.epochSeconds),sizeof(rec.epochSeconds));
				if(!in.good())return false;
				if(!readString(in,rec.ip))return false;
				u.history.push_back(rec);
			}

			state.uidByHandle[u.handle]=u.uid;
			state.usersById.emplace(u.uid,std::move(u));
		}
		return true;
	}

	bool save(const DbState &state) {
		std::ofstream out(path_,std::ios::binary|std::ios::trunc);
		if(!out.good())return false;
		const char magic[8]={'Q','C','H','A','T','D','B','1'};
		out.write(magic,8);
		if(!out.good())return false;
		out.write(reinterpret_cast<const char*>(&state.nextUid),sizeof(state.nextUid));
		if(!out.good())return false;
		u64 userCount=static_cast<u64>(state.usersById.size());
		out.write(reinterpret_cast<const char*>(&userCount),sizeof(userCount));
		if(!out.good())return false;
		for(const auto &kv:state.usersById){
			const User &u=kv.second;
			if(!writeString(out,u.handle))return false;
			if(!writeString(out,u.displayName))return false;
			out.write(reinterpret_cast<const char*>(&u.uid),sizeof(u.uid));
			if(!out.good())return false;
			u8 multi=u.allowMultiLogin?1u:0u;
			out.write(reinterpret_cast<const char*>(&multi),sizeof(multi));
			if(!out.good())return false;
			out.write(reinterpret_cast<const char*>(u.passwordHash.data()),u.passwordHash.size());
			if(!out.good())return false;
			u64 histCount=static_cast<u64>(u.history.size());
			out.write(reinterpret_cast<const char*>(&histCount),sizeof(histCount));
			if(!out.good())return false;
			for(const LoginRecord &rec:u.history){
				out.write(reinterpret_cast<const char*>(&rec.epochSeconds),sizeof(rec.epochSeconds));
				if(!out.good())return false;
				if(!writeString(out,rec.ip))return false;
			}
		}
		return true;
	}

private:
	std::string path_;

	static bool writeString(std::ofstream &out,const std::string &s) {
		u64 len=static_cast<u64>(s.size());
		out.write(reinterpret_cast<const char*>(&len),sizeof(len));
		if(!out.good())return false;
		if(len>0u){
			out.write(s.data(),static_cast<std::streamsize>(len));
			if(!out.good())return false;
		}
		return true;
	}

	static bool readString(std::ifstream &in,std::string &s) {
		u64 len=0;
		in.read(reinterpret_cast<char*>(&len),sizeof(len));
		if(!in.good())return false;
		if(len>1024u*1024u)return false; // sanity limit 1MB
		s.clear();
		if(len==0u)return true;
		s.resize(static_cast<std::size_t>(len));
		in.read(&s[0],static_cast<std::streamsize>(len));
		if(!in.good())return false;
		return true;
	}
};

} // namespace qchat

#endif
