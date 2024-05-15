#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    return operator+(isn, static_cast<uint32_t>(n));
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
// problem: (0, 0, 1 << 31) ????? FUCK
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    uint32_t offset = n - isn;
    uint32_t abs_offset = static_cast<uint32_t>(checkpoint);
    uint64_t res = (checkpoint & 0xFFFFFFFF00000000ULL) | offset;
    uint64_t const constexpr UINT32_RANGE = 1ULL << 32ULL;
    if (max(offset, abs_offset) - min(offset, abs_offset) >= (1U << 31U)) {
        if (abs_offset > offset)
            res += UINT32_RANGE;
        else if (res > UINT32_RANGE)
            res -= UINT32_RANGE;
    }
    return res;
}
