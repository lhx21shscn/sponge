#include "tcp_sender.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"
#include "tcp_config.hh"

#include <random>
#include <algorithm>
#include <queue>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity), _remote_ack(0), _remote_window_size(0), _timer(retx_timeout) {}

uint64_t TCPSender::bytes_in_flight() const { return _timer.bytes_in_flight(); }

/*
TCP发送数据的方式：
1. 主动触发，主动调用fill_window方法发送数据
2. 被动触发，收到ACK之后，如果有多余空间则调用fill_window方法继续发送
3. 超时重传，计时器超时重传数据
主动触发仅有一次！！！其他全靠被动触发和超时重传即可。
所以当调用fill_window方法时，window_size一定是新的。
*/
void TCPSender::fill_window() {
    // _timer:: add
    // _stream:: read
    // 当window_size为0时，当做1看待
    uint32_t cur_window_size = max(1U, static_cast<uint32_t>(_remote_window_size));

    while (cur_window_size > bytes_in_flight()) {
        size_t sz = min(TCPConfig::MAX_PAYLOAD_SIZE, _stream.remaining_capacity());
        _timer._bytes_in_flight += sz;

        // syn
        TCPSegment tcp_seg = TCPSegment();
        if (!_syn_flag) {
            tcp_seg.header().syn = true;
            _syn_flag = true;
        }

        // payload
        // note: 发生了一次值的拷贝 ByteStream->TCPSegment
        string payload = _stream.read(sz);
        tcp_seg.payload() = std::move(payload);

        // seq
        tcp_seg.header().seqno = wrap(_next_seqno, _isn);

        // fin
        // TODO: check
        if (_stream.eof() && tcp_seg.length_in_sequence_space() + _next_seqno > _stream.bytes_written()) {
            tcp_seg.header().fin = true;
        }

        // final
        _timer.add(_next_seqno, tcp_seg);
        _next_seqno += tcp_seg.length_in_sequence_space();
        _segments_out.push(tcp_seg); 
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // _timer:: peek, pop
    // _stream:: pop
    if (ackno.raw_value() > _remote_ack.raw_value()) {
        _remote_ack = ackno;
        _remote_window_size = window_size;
        _timer.delete_acked_segment(unwrap(ackno, _isn, next_seqno_absolute()));
        _timer.restart_timer();
        fill_window();
    }
    // else if (ackno.raw_value() == _remote_ack.raw_value()) {
    //     // ack不变，window_size递增，如果远端window_size大于当前的，说明远端更加新。
    //     _remote_window_size = max(window_size, _remote_window_size);
    //     fill_window();
    // }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // 计时器开启
    if (!_timer.data().empty()) {
        _timer._time_passed += ms_since_last_tick;
        // 计时器超时
        if (_timer._rto <= _timer._time_passed) {
            _segments_out.push(_timer.data().begin()->second);
            _timer._rto *= 2;
            _timer._retry ++;
            _timer._time_passed = 0;
        }
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { 
    return _timer.consecutive_retransmissions(); 
}

void TCPSender::send_empty_segment() {
    TCPSegment seg = TCPSegment();
    _segments_out.push(seg);
}

// Definition of TCPSender::_RetransmissionTimer:: 
TCPSender::_RetransmissionTimer::_RetransmissionTimer(unsigned int _init_rto)
                                    : _rto(0), _init_rto(_init_rto) {};

void TCPSender::_RetransmissionTimer::delete_acked_segment(uint64_t ackno) {
    for (auto iter = _aux_storage.begin(); iter != _aux_storage.end(); /* no op */) {
        if (iter->first <= ackno) {
            _bytes_in_flight -= iter->second.payload().size();
            _aux_storage.erase(iter);
        }
        else break;
    }
}

void TCPSender::_RetransmissionTimer::restart_timer() {
    _time_passed = 0;
    _rto = _init_rto;
    _retry = 0;
}