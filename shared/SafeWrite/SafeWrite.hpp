#pragma once

#include <windows.h>
#include <cstdint>

void __fastcall SafeWrite8(SIZE_T addr, SIZE_T data);
void __fastcall SafeWrite16(SIZE_T addr, SIZE_T data);
void __fastcall SafeWrite32(SIZE_T addr, SIZE_T data);
void __fastcall SafeWriteBuf(SIZE_T addr, const void* data, SIZE_T len);

void __fastcall WriteRelJump(SIZE_T jumpSrc, SIZE_T jumpTgt);
void __fastcall WriteRelCall(SIZE_T jumpSrc, SIZE_T jumpTgt);
void __fastcall ReplaceCall(SIZE_T jumpSrc, SIZE_T jumpTgt);
void __fastcall PatchMemoryNop(ULONG_PTR Address, SIZE_T Size);

template <typename T>
void __fastcall WriteRelCall(SIZE_T jumpSrc, T jumpTgt) {
	WriteRelCall(jumpSrc, (SIZE_T)jumpTgt);
}

template <typename T>
void __fastcall WriteRelJump(SIZE_T jumpSrc, T jumpTgt) {
	WriteRelJump(jumpSrc, (SIZE_T)jumpTgt);
}

template <typename T>
void __fastcall ReplaceCall(SIZE_T jumpSrc, T jumpTgt) {
	ReplaceCall(jumpSrc, (SIZE_T)jumpTgt);
}

static inline SIZE_T GetRelJumpAddr(SIZE_T jumpSrc) {
	return *(SIZE_T*)(jumpSrc + 1) + jumpSrc + 5;
}

// Stores the overwritten call target for later use
class CallDetour {
	SIZE_T overwritten_addr = 0;
public:
	template <typename T>
	void __fastcall WriteDetour(SIZE_T jumpSrc, T jumpTgt) {
		if (*reinterpret_cast<uint8_t*>(jumpSrc) != 0xE8) {
			return;
		}
		overwritten_addr = GetRelJumpAddr(jumpSrc);
		::ReplaceCall(jumpSrc, (SIZE_T)jumpTgt);
	}

	[[nodiscard]] SIZE_T GetOverwrittenAddr() const { return overwritten_addr; }
};
