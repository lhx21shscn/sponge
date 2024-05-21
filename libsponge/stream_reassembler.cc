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
    if (_output.input_ended()) return;
    if (data.empty()) {
        // 下面这段代码是为了解决测试用例中利用空字符串标识结束的情况
        if (eof && !~_eof_index) {
            // 很坑！
            // TODO: 这里和SYN FIN数据为空但是要占一个序列号有关！！
            // 传空字符串，index=0，eof=1时，是直接结束
            // 传空字符串，index>0，eof=1时，是在index-1结束
            if (index)
                _eof_index = index - 1;
            else
                _eof_index = 0;
        }
        if (!_eof_index || (static_cast<ssize_t>(_ack) > _eof_index && ~_eof_index)) {
            _output.end_input();
            _aux_storage.clear();
            _unassembler_bytes = 0;
        }
        return;
    }

    size_t const st = index;
    size_t const ed = index + data.size() - 1;
    if (eof && !~_eof_index) {
        _eof_index = ed;
    }

    /*
    对两种情况的排除：
    1. st + ed < ack: 不会进入下面的if语句
    2. st > ack + capacity：下面的if语句无法处理，这里需要额外进行排除
    */
    if (st >= _ack + _capacity) {
        return;
    }

    // 空字符串不进入下面两个语句
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
        ssize_t concat_len = -1;
        auto str_iter = _aux_storage.upper_bound(ed + 1);
        if (str_iter != _aux_storage.begin())
            str_iter--;
        if (str_iter != _aux_storage.end() && str_iter->first <= ed + 1) {
            size_t p = str_iter->first, sz = str_iter->second.size();
            if (p + sz > ed + 1) {
                // "状态约定"第三条保证字符串和字符串之间一定有间隔，所以这里只需要合并一个字符串。
                concat_data = data.substr() + str_iter->second.substr(ed + 1 - p);
                concat_len = len_w + (sz - ed - 1 + p);
            }
            /* --------------------------------------------------
            此时直接删除这些数据是否正确？是否完美？完全正确，非常完美！
            可以分情况讨论：
            1. ByteStream写满了，此时整个结构的容量已经到达capacity上限了，
               除了ByteStream Buffer中的内容，其他所有都要删除此时，_write_data_to_ByteStream会执行clear
            2. ByteStream未能写满，此时删除的这部分已经全部进入到ByteStream的buffer中了，可以执行删除。
            --------------------------------------------------- */
            for (auto iter = _aux_storage.begin(); iter != str_iter; /*no op*/) {
                _unassembler_bytes -= iter->second.size();
                iter = _aux_storage.erase(iter);
            }
            _unassembler_bytes -= str_iter->second.size();
            str_iter = _aux_storage.erase(str_iter);
        }

        // 写入 ByteStream
        if (concat_len > 0)
            _write_data_to_ByteStream(concat_data, concat_data.size() - concat_len, concat_len, index);
        else
            _write_data_to_ByteStream(data, data.size() - len_w, len_w, index);

        // 到达EOF
        if (~_eof_index && static_cast<ssize_t>(_ack) > _eof_index) {
            _output.end_input();
            _aux_storage.clear();
            _unassembler_bytes = 0;
        }
    // case 2: st > _ack
    // just push string to aux_storage and maintain it;
    } else if (st > _ack) {
        // 找到p < st的最后一个字符串
        auto str_iter = _aux_storage.lower_bound(st);
        if (str_iter != _aux_storage.begin()) {
            str_iter --;
        }
        // BUG: 有可能找不到p < st的但是能找到下面几种if情况的(符合情况的需要处理)，此时直接将str_iter置为begin
        if (str_iter != _aux_storage.end() && str_iter->first >= st) str_iter = _aux_storage.begin();

        string s1, s2;
        if (str_iter != _aux_storage.end()) {
            for (auto iter = str_iter; iter != _aux_storage.end(); /*no op*/) {
                // p代表当前字符的起始索引，sz代表当前字符串的长度
                size_t p = iter->first, sz = iter->second.size();

                if (p < st) {
                    if (p + sz < st) {
                        iter ++;
                        continue;
                    }
                    else if (p + sz < ed + 1) {
                        s1 = std::move(iter->second.substr(0, st - p));
                        _unassembler_bytes -= sz;
                        iter = _aux_storage.erase(iter);
                        continue;
                    } else {
                        // 数据被包裹住了，直接返回
                        return;
                    }
                } else if (p >= st && p <= ed + 1) {
                    if (p + sz <= ed + 1) {
                        // 被数据包裹住
                        _unassembler_bytes -= sz;
                        iter = _aux_storage.erase(iter);
                        continue;
                    } else {
                        s2 = std::move(iter->second.substr(ed + 1 - p));
                        _unassembler_bytes -= sz;
                        iter = _aux_storage.erase(iter);
                        break;
                    }
                } else {
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
        _unassembler_bytes = 0;
        _aux_storage.clear();
    }
}
