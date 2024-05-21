#include "tcp_receiver.hh"
#include "wrapping_integers.hh"

#include <optional>

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    TCPHeader const &header = seg.header();
    if (!_syn_flag) {
        // 处在LISTEN状态时，丢弃一些非SYN包
        if (!header.syn)
            return;
        _syn_flag = true;
        _isn = header.seqno;
    }

    /*
    TCP中的三种序列号：1. TCP头里的相对序列号seq 2. 绝对序列号abs_seq 3. ByteStream中的索引index
    下面的代码完成了seq -> abs_seq -> index的过程
    */
    size_t index = unwrap(header.seqno, _isn, _reassembler.stream_out().bytes_written() + 1ULL);
    /*  
    这行代码进行了绝对序列号到字节流索引的转换，但是这里有bug
    当绝对序列号为0，且header.syn=1时，溢出了。
    原代码：index = index - 1 + header.syn;
    */
    if (index || header.syn) {
        index = index - 1 + header.syn;
    } else {
        // 发送了上述注释的情况，发送了错误的包。
        return;
    }
    
    _reassembler.push_substring(seg.payload().copy(), index, header.fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const { 
    // LISTEN 状态
    if ( !_syn_flag ) {
        return nullopt;
    }
    // SYN 占用一个序列号 FIN 占用一个序列号
    uint64_t ack_no = _reassembler.stream_out().bytes_written() + 1ULL;
    if ( _reassembler.stream_out().input_ended() ) {
        // FIN_RECV 状态
        ack_no ++;
    }
    return { wrap( ack_no, WrappingInt32( _isn ) ) };
}

size_t TCPReceiver::window_size() const { 
    return _reassembler.stream_out().remaining_capacity();
}
