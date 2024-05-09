#include "stream_reassembler.hh"

#include <algorithm>
#include <vector>
#include <string>
#include <deque>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity), _capacity(capacity), _eof_index(-1),
    _ack(0), _unassembler_bytes(0), _aux_storage() {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.

// 状态约定！
// 每次调用push_substring都要保证StreamReassembler对象满足以下性质：
// 1. ByteStream 和 aux_storage中存储的字节数目不超过 capacity
// 2. aux_storage中存储的字符串的起始索引大于_ack
// 3. aux_storage中存储的字符串不重合且字符串之间一定有间隙，不能将两个相邻的字符串合并。(不能出现字符串a索引为[9, 18], b索引为[19, 23], 因为此时可以将字符串合并为[9, 23])
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // 接收到EOF or data为空
    if (_output.input_ended() || data.empty()) return;

    size_t const st = index;
    size_t const ed = index + data.size() - 1;
    if (eof && !~_eof_index) {
        _eof_index = ed;
    }

    // case 1: st <= ack and ed >= ack
    // push data to ByteStream and maintain the _aux_storage
    if (st <= _ack && ed >= _ack) {
        size_t const len_w = ed - _ack + 1;

        /* -----------------------------------------------------------------
        删除为什么要放到写入前？
        写入时如果内存超过capacity会从后向前删数据，
        这里先删除前面的无用数据就尽可能让写入的时候删除的少一点
        --------------------------------------------------------------------*/
        // 删除重合的data数据
        // 维护 aux_storage, 要求字符串起始idx大于_ack, 且字符串无重合
        string concat_data;
        size_t concat_len = -1;
        auto str_iter = lower_bound(_aux_storage.begin(), _aux_storage.end(), make_pair(ed, string()), greater<pair<size_t, string>>());
        if (str_iter != _aux_storage.end()) {
            size_t p = str_iter->first, sz = str_iter->second.size();
            if (p + sz > ed + 1) {
                // "状态约定"第三条保证字符串和字符串之间一定有间隔，所以这里只需要合并一个字符串。
                concat_data = data + str_iter->second.substr(ed + 1 - p);
                concat_len = len_w + (sz - ed - 1 + p);
            }
            /* --------------------------------------------------
            此时直接删除这些数据是否正确？是否完美？完全正确，非常完美！
            可以分情况讨论：
            1. ByteStream写满了，此时整个结构的容量已经到达capacity上限了，
               除了ByteStream Buffer中的内容，其他所有都要删除此时，_write_data_to_ByteStream会执行clear
            2. ByteStream未能写满，此时删除的这部分已经全部进入到ByteStream的buffer中了，可以执行删除。
            --------------------------------------------------- */
            _aux_storage.erase(_aux_storage.begin(), str_iter);
        }

        // 写入 ByteStream
        if (~concat_len) // concat_len == -1
            _write_data_to_ByteStream(concat_data, concat_data.size() - concat_len, concat_len, index);
        else
            _write_data_to_ByteStream(data, data.size() - len_w, len_w, index);

        // 到达EOF
        if (static_cast<ssize_t>(_ack) > _eof_index) {
            _output.end_input();
            _aux_storage.clear();
        }
    // case 2: st > _ack
    // just push string to aux_storage and maintain it;
    } else if (st > _ack) {
        // 找到p < st的最后一个字符串
        auto str_iter = upper_bound(_aux_storage.begin(), _aux_storage.end(), make_pair(st, string()), greater<pair<size_t, string>>());
        // BUG: 有可能找不到p < st的但是能找到下面几种if情况的(符合情况的需要处理)，此时直接将str_iter置为begin
        if (str_iter == _aux_storage.end()) str_iter = _aux_storage.begin();

        string s1, s2;
        if (str_iter != _aux_storage.end()) {
            for (auto iter = str_iter; iter != _aux_storage.end(); /*no op*/) {
                // p代表当前字符的起始索引，sz代表当前字符串的长度
                size_t p = iter->first, sz = iter->second.size();
                if (p >= st && p <= ed + 1 && p + sz > ed + 1) {
                    // 需要截断，仅会在末端可能出现一次
                    s2 = std::move(iter->second.substr(ed + 1 - p));
                    _unassembler_bytes -= sz;
                    iter = _aux_storage.erase(iter);
                    break;
                } else if (p >= st && p + sz <= ed + 1) {
                    // 需要删除
                    _unassembler_bytes -= sz;
                    iter = _aux_storage.erase(iter);
                } else if (p < st && p + sz >= st) {
                    // 需要截断，仅会在始端可能出现一次
                    s1 = std::move(iter->second.substr(0, st - p));
                    _unassembler_bytes -= sz;
                    iter = _aux_storage.erase(iter);
                } else if (p <= st && p + sz >= ed + 1) {
                    // 数据被包裹住了，直接返回即可
                    return;
                }
                else if (p > ed + 1) {
                    // 
                    break;
                }
            }
        }

        // TODO: capacity
        if (s1.empty() && s2.empty()) {
            _unassembler_bytes += data.size();
            _aux_storage.insert(make_pair(st, data));
        } else {
            size_t st_r = st - s1.size();
            s1.reserve(s1.size() + data.size() + s2.size());
            s1.append(data).append(s2);
            _unassembler_bytes += s1.size();
            _aux_storage.insert(make_pair(st_r, std::move(s1)));
        }
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembler_bytes; }

bool StreamReassembler::empty() const { return _aux_storage.empty(); }

// 包装ByteStream::write方法，同步更新状态变量，保证容量限制
void StreamReassembler::_write_data_to_ByteStream(string const &data, size_t pos, size_t len, size_t index) {
    size_t sz = _output.remaining_capacity();
    size_t len_w = min(sz, len);
    // 向ByteStream中输入尽可能多的数据
    _output.write(data.substr(pos, len_w));
    _ack = index + pos + len_w;

    // 检查内存是否超过capacity,如果超过则进行从后向前删除
    if (_output.remaining_capacity()) {
        size_t buffer_size = _output.buffer_size();
        while (buffer_size + _unassembler_bytes > _capacity) {
            auto iter = _aux_storage.rbegin();
            _unassembler_bytes -= iter->second.size();
            _aux_storage.erase(iter->first);
        }
    } else {
        _aux_storage.clear();
    }
}
