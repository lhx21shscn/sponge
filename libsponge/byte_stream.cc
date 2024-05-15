#include "byte_stream.hh"

#include <deque>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity): 
    _capacity(capacity), _capacity_now(capacity) {};

size_t ByteStream::write(const string &data) {
    if (input_ended() || error())
        return 0U;
    
    size_t s = data.size();
    size_t len = min( s, remaining_capacity() );

    if (!len) return 0U;

    _num_bytes_pushed += len;
    _capacity_now -= len;

    for (size_t i = 0; i < len; ++ i)
        _data_channel.push_back(data.at(i));

    return len;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t len_r = min( len, buffer_size() );
    return string( _data_channel.begin(), _data_channel.begin() + len_r );
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) { 
    size_t len_p = min( len, buffer_size() );

    _num_bytes_poped += len;
    _capacity_now += len;

    for (size_t i = 0; i < len_p; ++ i) {
        _data_channel.pop_front();
    }
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    // method "pop_output" and "peek_output" will validate the data range.
    std::string res = peek_output(len);
    pop_output(len);
    return std::move(res);
}