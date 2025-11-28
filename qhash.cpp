#include <array>
#include <cstdint>
#include <cstring>
#include <bit>

namespace qhash {

using u8  = std::uint8_t;
using u64 = std::uint64_t;

namespace detail {

static constexpr u64 RC[24]={
	0x0000000000000001ULL,0x0000000000008082ULL,
	0x800000000000808aULL,0x8000000080008000ULL,
	0x000000000000808bULL,0x0000000080000001ULL,
	0x8000000080008081ULL,0x8000000000008009ULL,
	0x000000000000008aULL,0x0000000000000088ULL,
	0x0000000080008009ULL,0x000000008000000aULL,
	0x000000008000808bULL,0x800000000000008bULL,
	0x8000000000008089ULL,0x8000000000008003ULL,
	0x8000000000008002ULL,0x8000000000000080ULL,
	0x000000000000800aULL,0x800000008000000aULL,
	0x8000000080008081ULL,0x8000000000008080ULL,
	0x0000000080000001ULL,0x8000000080008008ULL
};

static constexpr int R[5][5]={
	{  0, 36,  3, 41, 18},
	{  1, 44, 10, 45,  2},
	{ 62,  6, 43, 15, 61},
	{ 28, 55, 25, 21, 56},
	{ 27, 20, 39,  8, 14}
};

// Keccak-f[1600], little-endian 64-bit lanes
inline void keccakf(u64 s[25]) {
	for(int rnd=0;rnd<24;++rnd){
		// θ
		u64 C[5];
		for(int x=0;x<5;++x)
			C[x]=s[x]^s[x+5]^s[x+10]^s[x+15]^s[x+20];

		u64 D[5];
		for(int x=0;x<5;++x)
			D[x]=C[(x+4)%5]^std::rotl(C[(x+1)%5],1);

		for(int x=0;x<5;++x)
		for(int y=0;y<5;++y)
			s[x+5*y]^=D[x];

		// ρ and π
		u64 B[25];
		for(int x=0;x<5;++x)
		for(int y=0;y<5;++y){
			int nx=y;
			int ny=(2*x+3*y)%5;
			B[nx+5*ny]=std::rotl(s[x+5*y],R[x][y]);
		}

		// χ
		for(int y=0;y<5;++y){
			u64 b0=B[5*y+0];
			u64 b1=B[5*y+1];
			u64 b2=B[5*y+2];
			u64 b3=B[5*y+3];
			u64 b4=B[5*y+4];
			s[5*y+0]=b0^((~b1)&b2);
			s[5*y+1]=b1^((~b2)&b3);
			s[5*y+2]=b2^((~b3)&b4);
			s[5*y+3]=b3^((~b4)&b0);
			s[5*y+4]=b4^((~b0)&b1);
		}

		// ι
		s[0]^=RC[rnd];
	}
}

// SHA3-512 parameters
static constexpr std::size_t SHA3_512_RATE   = 72;  // bytes
static constexpr std::size_t SHA3_512_DIGEST = 64;  // bytes

} // namespace detail

// bitlen is the number of bits in *data.
// For best portability, prefer bitlen being a multiple of 8; sub-byte
// messages are supported but slightly slower.
std::array<u8,64> sha3_512(const void* data,std::uint64_t bitlen){
	using namespace detail;

	const auto* in=static_cast<const u8*>(data);
	const std::uint64_t bytelen=bitlen/8;
	const unsigned rembits=static_cast<unsigned>(bitlen%8);

	u64 st[25]{};

	// Absorb full-rate blocks
	std::uint64_t offset=0;
	while(bytelen-offset>=SHA3_512_RATE){
		// XOR block into state, as little-endian lanes
		for(std::size_t i=0;i<SHA3_512_RATE/8;++i){
			u64 lane=0;
			std::memcpy(&lane,in+offset+8*i,8);
			st[i]^=lane;
		}
		keccakf(st);
		offset+=SHA3_512_RATE;
	}

	// Build final padded block(s)
	// We work on a zeroed temp block, then XOR into state.
	std::array<u8,SHA3_512_RATE> block{};
	const std::uint64_t remaining=bytelen-offset;
	if(remaining>0)
		std::memcpy(block.data(),in+offset,static_cast<std::size_t>(remaining));

	if(rembits){
		// Take remaining bits from the next input bit position
		// Bits are taken LSB-first within a byte.
		const u8 mask=(1u<<rembits)-1u;
		const u8 last_byte=in[bytelen] & mask;
		block[remaining]|=last_byte;
	}

	// Domain separation for SHA3: 0x06, then pad10*1 (final bit 0x80)
	block[remaining] ^= 0x06u;
	block[SHA3_512_RATE-1] ^= 0x80u;

	// Absorb final block
	for(std::size_t i=0;i<SHA3_512_RATE/8;++i){
		u64 lane=0;
		std::memcpy(&lane,block.data()+8*i,8);
		st[i]^=lane;
	}
	keccakf(st);

	// Squeeze digest
	std::array<u8,SHA3_512_DIGEST> out{};
	std::size_t out_off=0;
	std::size_t rate=SHA3_512_RATE;

	while(out_off<SHA3_512_DIGEST){
		const std::size_t chunk
			= SHA3_512_DIGEST-out_off < rate
			? SHA3_512_DIGEST-out_off
			: rate;
		for(std::size_t i=0;i<chunk/8;++i){
			u64 lane=st[i];
			std::memcpy(out.data()+out_off+8*i,&lane,8);
		}
		if(chunk%8){
			u64 lane=st[chunk/8];
			std::memcpy(out.data()+out_off+chunk/8*8,&lane,chunk%8);
		}
		out_off+=chunk;
		if(out_off<SHA3_512_DIGEST){
			keccakf(st);
		}
	}

	return out;
}

} // namespace qhash
