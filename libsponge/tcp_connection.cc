#include "tcp_connection.hh"

#include <iostream>
#include <optional>
#include <limits>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

    // TODO: 判断is_alive, 连接是否关闭
void TCPConnection::segment_received(const TCPSegment &seg) {
    _time_since_last_segment_received = 0;
    if (seg.header().rst) {
        _abort_connection(false);
        return;
    }

    // Receiver: 更新 ackno and winsize 并将payload放入ByteStream中
    _receiver.segment_received(seg);
    optional<WrappingInt32> ackno = _receiver.ackno();

    // Sender: 接受ACK，并根据远端window_size继续发包。
    size_t start_sz = _sender.segments_out().size();
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
    }
    size_t end_sz = _sender.segments_out().size();

    /*
    大部分情况下，ack_received会自动发包，但是在
    1. 三次握手中的第二次握手(发送SYN+ACK)
    2. Keep-Alive报文
    3. 正常数据传输时，由于远端window_size过小获取本地缓存无数据导致fill_window没有发包。
    这些情况需要特判并补充发包
    */

    // 情况1：三次握手中的第二次握手，由于第一个报文是纯SYN，不带ACK，所以ack_received方法没有调用，导致无法发包。
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV 
        && TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
        connect(); // 发送SYN+ACK
        return;
    }

    // 情况2：keep-alive报文，要保证长时间没有数据传输的tcp连接一直存在，则需要发送keep-alive报文。
    // if (_receiver.ackno().has_value() && (seg.length_in_sequence_space() == 0)
    //     && seg.header().seqno == _receiver.ackno().value() - 1) {
    //     _sender.send_empty_segment();
    // }

    // 情况3：正常数据的传输中，由于没有需要发送的数据或者窗口大小不够导致fill_window没有发送ACK
    if (ackno.has_value() && start_sz == end_sz && seg.length_in_sequence_space() != 0) {
        _sender.send_empty_segment();
    }

    /*
    四次挥手过程中，需要额外判断两种情况
    1. _linger_after_streams_finish变量(标识当前是主动关闭连接的还是被动关闭连接的)
    2. 如果此时TCP连接正常结束了，且是被动关闭连接的一方，可以直接shutdown。
    */

    // 情况1：判断当前的一段是否是被动关闭连接的一方(不需要TIME_WAIT)
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED) {
        // 先收到FIN此时是被动关闭连接方，不需要TIME_WAIT状态
        _linger_after_streams_finish = false;
    }

    // 情况2：TCP连接结束
    if (!_linger_after_streams_finish &&
        TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED) {
        _abort_connection(false);
    }
    _transmitting_and_add_ack_windowsize_for_segments();
}

bool TCPConnection::active() const { return _is_active; }

size_t TCPConnection::write(const string &data) {
    size_t num_bytes_writen = _sender.stream_in().write(data);
    _sender.fill_window();
    _transmitting_and_add_ack_windowsize_for_segments();
    return num_bytes_writen;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _time_since_last_segment_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);

    // forced showdown
    if (_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS) {
        _abort_connection(true);
        return;
    }

    _transmitting_and_add_ack_windowsize_for_segments();

    // cleanly shutdown
    if (_linger_after_streams_finish && _time_since_last_segment_received > 10 * _cfg.rt_timeout
            && TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED
            && TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV) {
        _abort_connection(false);
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    // 此时要立刻调用fill_window方法，不然可能导致TCP连接陷入停滞状态。
    // 具体：如果收发双方的ByteSteam中此时已经没有发送的数据了(write方法不会调用，数据也全部被ACK了)
    //      如果此时不调用fill_window，FIN标志就永远发不出去了。
    //      当然，如果此时仍有数据在ByteStream中需要发送，FIN会自动发送出去。
    _sender.fill_window();
    _transmitting_and_add_ack_windowsize_for_segments();
}

/*
主动连接方主动调用connect方法，发送SYN报文。
被动连接方收到报文后在segment_received方法中被动调用connect方法，发送SYN+ACK报文。
*/
void TCPConnection::connect() {
    // connect方法应该仅被调用一次，先判断是否处在CLOSE状态
    if (_sender.next_seqno_absolute() == 0 /* && _receiver??*/) {
        // 第一次调用，将SYN报文丢到发送队列里
        _sender.fill_window();
        _transmitting_and_add_ack_windowsize_for_segments();
    }
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the 
            _abort_connection(true);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

/*
从_sender的segments_out中取出所有的报文并加入当前的ackno和window_size
*/
void TCPConnection::_transmitting_and_add_ack_windowsize_for_segments() {
    queue<TCPSegment>& que = _sender.segments_out();
    while(!que.empty()) {
        TCPSegment &temp = que.front();
        _set_ack_windowsize_for_segments(temp, _receiver.ackno().value(), _receiver.window_size());
        _segments_out.push(temp);
        que.pop();
    }
}

void TCPConnection::_abort_connection(bool is_rst_sent) {
    is_rst_sent = false;
    cerr << is_rst_sent << endl;
    return;
}

