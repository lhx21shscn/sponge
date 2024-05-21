#include "tcp_sender.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"
#include "tcp_config.hh"

#include <random>
#include <algorithm>
#include <iostream>
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
    , _stream(capacity), _timer(retx_timeout), _remote_ack(_isn) {}

uint64_t TCPSender::bytes_in_flight() const { return _timer.bytes_in_flight(); }

/*
TCP发送数据的方式：
1. 主动触发，主动调用fill_window方法发送数据
2. 被动触发，收到ACK之后，如果有多余空间则调用fill_window方法继续发送
3. 超时重传，计时器超时重传数据
*/


/*
保证：对应一个ack的值，fill_window具有幂等性(ack的值被更新，调用多少次fill_window都行)。
*/
void TCPSender::fill_window() {
    // 当window_size为0时，当做1看待
    uint32_t cur_window_size = max(1U, static_cast<uint32_t>(_remote_window_size));
    while (cur_window_size > bytes_in_flight()) {
        /*
        有两个地方很坑：
        1. 判断是否要发送，要看当下状况(_strem, bytes_in_flight, remote_ack, remote_window_size)是否需要发送占有序列号的包，
           也就是SYN、数据、FIN都不需要发送时再退出。
           这里采取的方式是：只要接受端窗口有剩余容量就进入循环生成TCP报文，如果报文不占用任何序列号，就不发并退出。
        2. 这里生成了max_sent_len，max_sent_len是发送TCP报文占用序列号的最大值，也包括了FIN、SYN。
           也就是要先看是否需要SYN，如果需要则payload部分能占用的序列号要减去SYN标志占用的序列号。再看是否可以生成FIN标志
        */
        size_t max_sent_len = cur_window_size > bytes_in_flight() ? cur_window_size - bytes_in_flight() : 0;

        TCPSegment tcp_seg = TCPSegment();

        // seq
        tcp_seg.header().seqno = wrap(_next_seqno, _isn);

        // syn
        if (!_syn_flag && max_sent_len) {
            tcp_seg.header().syn = true;
            _syn_flag = true;
            max_sent_len --;
        }

        // payload
        // note: 发生了一次值的拷贝 ByteStream->TCPSegment
        size_t sz = min(max_sent_len, _stream.buffer_size());
        sz = min(sz, TCPConfig::MAX_PAYLOAD_SIZE);
        string payload = _stream.read(sz);
        tcp_seg.payload() = std::move(payload);
        max_sent_len -= sz;

        // fin
        if (!_fin_flag && _stream.eof() && max_sent_len && tcp_seg.length_in_sequence_space() + _next_seqno > _stream.bytes_written()) {
            tcp_seg.header().fin = true;
            _fin_flag = true;
            max_sent_len --;
        }

        // final
        size_t seg_len = tcp_seg.length_in_sequence_space();
        if (seg_len) {
            _timer.add(_next_seqno, tcp_seg);
            _next_seqno += seg_len;
            _segments_out.push(tcp_seg);
            _timer._bytes_in_flight += seg_len;
        // 如果不占用序列号直接return, 否则会无限循环
        } else return;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    /*
    ackno不仅要大于当前的，还有合理(是未发送的字节都ack了)
    */
    uint64_t new_abs_ack = unwrap(ackno, _isn, next_seqno_absolute());
    uint64_t old_abs_ack = unwrap(_remote_ack, _isn, next_seqno_absolute());
    if (new_abs_ack > old_abs_ack && new_abs_ack <= next_seqno_absolute()) {
        _remote_ack = ackno;
        _remote_window_size = window_size;
        _timer.delete_acked_segment(new_abs_ack);
        _timer._time_passed = 0;
        _timer._rto = _initial_retransmission_timeout;
        _timer._retry = 0;
        fill_window();
    }
    else if (new_abs_ack == old_abs_ack) {
        // ack不变，window_size递增，如果远端window_size大于当前的，说明远端更加新。
        _remote_window_size = max(window_size, _remote_window_size);
        fill_window();
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // 计时器开启
    if (!_timer.data().empty()) {
        _timer._time_passed += ms_since_last_tick;
        // 计时器超时
        if (_timer._rto <= _timer._time_passed) {
            _segments_out.push(_timer.data().begin()->second);
            // 此时如果不是0窗口，才倍增RTO！！！！
            // 窗口大小不要初始化成0，不然三次握手中第一次握手发SYN的时候也不倍增RTO了
            if (_remote_window_size)
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
    seg.header().seqno = next_seqno();
    _segments_out.push(seg);
}

// Definition of TCPSender::_RetransmissionTimer:: 
TCPSender::_RetransmissionTimer::_RetransmissionTimer(unsigned int init_rto)
                                    : _rto(init_rto), _init_rto(init_rto) {};

void TCPSender::_RetransmissionTimer::delete_acked_segment(uint64_t ackno) {
    for (auto iter = _aux_storage.begin(); iter != _aux_storage.end(); /* no op */) {
        TCPSegment &seg = iter->second;
        size_t seg_abs_len = seg.length_in_sequence_space();
        if (ackno >= iter->first + seg_abs_len) {
            // 报文中所有内容都被ack
            _bytes_in_flight -= seg_abs_len;
            iter = _aux_storage.erase(iter);
        } 
        // else if (ackno > iter->first) {
        //     // 报文中部分被接受，需要截断
        //     _bytes_in_flight -= ackno - iter->first;
        //     iter->second.payload().remove_prefix(ackno - iter->first);
        //     _aux_storage.insert(make_pair(ackno, iter->second));
        //     _aux_storage.erase(iter);
        //     break;
        // }
        else break;
    }
}
