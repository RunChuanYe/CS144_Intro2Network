#include "stream_reassembler.hh"

#include <iostream>
#include <stdexcept>
// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity), _capacity(capacity), _substrs(), _substrs_size(0) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // three steps in total:
    // 1. remove the duplicate chars in data
    // 2. get empty space for data_no_dup
    // 3. put _sub_strs into the _output
    
    // remove the substr from data that has been written to ByteStream
    // ByteStream index start from 0
    string data_no_dup = data;
    // data_no_dup: [index_begin, index_end]
    size_t index_begin = index;
    if (_output.bytes_written() > index) {
        // duplicate str [index, bytes_written-1] in Stream
        size_t start_index = _output.bytes_written() - index;
        if (start_index >= data_no_dup.size()) {
            // data all duplicate, just return
            return;
        } else {
            // save: data [_output.bytes_written() - index, data.size() - 1]
            data_no_dup = data_no_dup.substr(start_index);
            index_begin = _output.bytes_written();
        }
    }

    size_t index_end = index_begin + data_no_dup.size() - 1;
    bool eof_no_dup = eof;

    // for ([]) case
    bool breakdown = false;
    Segment breakdown_data;

    // remove the substr from data that has been stored in the sub_strs
    for (auto it = _substrs.begin(); it != _substrs.end(); ++it) {
        // all duplicate
        if (data_no_dup.size() == 0)
            return;
        // curr_str range: [curr_begin, curr_end]
        size_t curr_begin = it->second.index;
        size_t curr_end = it->second.data.size() + curr_begin - 1;
        if (index_end < curr_begin || curr_end < index_begin) {
            // no duplicate
            continue;
        } else if (curr_begin <= index_begin && curr_end >= index_end) {
            // [()]
            // completely duplicate, return
            return;
        } else if (curr_begin <= index_begin && curr_end < index_end) {
            // []: curr, (): index
            // [(])
            data_no_dup = data_no_dup.substr(curr_end - index_begin + 1);
            index_begin = curr_end + 1;
        } else if (curr_begin > index_begin && index_end <= curr_end) {
            // ([)]
            index_end = curr_begin - 1;
            data_no_dup = data_no_dup.substr(0, index_end - index_begin + 1);
            eof_no_dup = false;
        } else {
            // ([])
            breakdown = true;
            // part 1
            breakdown_data.eof = eof_no_dup;
            breakdown_data.data = data_no_dup.substr(curr_end - index_begin + 1);
            breakdown_data.index = curr_end + 1;
            // part 2
            data_no_dup = data_no_dup.substr(0, curr_begin - index_begin);
            index_end = curr_begin - 1;
            eof_no_dup = false;
            break;
        }
    }

    // recursive solve
    if (breakdown) {
        push_substring(data_no_dup, index_begin, eof_no_dup);
        push_substring(breakdown_data.data, breakdown_data.index, breakdown_data.eof);
        return;
    }

    // put the no dup data to the map
    // any point in the proc, satisfy the capacity limit!
    if (data_no_dup.size() > empty_size()) {
        // find the max index
        if (empty()) {
            data_no_dup = data_no_dup.substr(0, empty_size());
        } else {
            size_t max_index = _substrs.begin()->second.index;
            if (max_index < index_begin)
                return;  // curr data will be uesd in future
            // only process last data seg is enough ?
            if ((_substrs.begin()->second.data.size() + empty_size()) > data_no_dup.size()) {
                _substrs.begin()->second.data = _substrs.begin()->second.data.substr(
                    0, _substrs.begin()->second.data.size() + empty_size() - data_no_dup.size());
                _substrs_size -= data_no_dup.size();
                _substrs.begin()->second.eof = false;
            } else {
                // continue for empty space
                _substrs_size -= _substrs.begin()->second.data.size();
                _substrs.erase(_substrs.begin());
                // pop the max index str until the data_no_dup can be add to _substrs
                while (empty_size() < data_no_dup.size()) {
                    if (empty()) {
                        data_no_dup = data_no_dup.substr(0, empty_size());
                        break;
                    }
                    max_index = _substrs.begin()->second.index;
                    if (max_index < index_begin) {
                        // only empty() space for curr data
                        data_no_dup = data_no_dup.substr(0, empty_size());
                        break;
                    }
                    if ((_substrs.begin()->second.data.size() + empty_size()) > data_no_dup.size()) {
                        _substrs.begin()->second.data = _substrs.begin()->second.data.substr(
                            0, _substrs.begin()->second.data.size() + empty_size() - data_no_dup.size());
                        _substrs_size -= data_no_dup.size();
                    } else {
                        _substrs_size -= _substrs.begin()->second.data.size();
                        _substrs.erase(_substrs.begin());
                    }
                }
            }
        }
    }
    _substrs[index_begin] = Segment(eof_no_dup, data_no_dup, index_begin);
    _substrs_size += data_no_dup.size();

    // find the substr in order write to _ouput
    while (_substrs.find(_output.bytes_written()) != _substrs.end()) {
        // size_wirte
        size_t size_write = _output.remaining_capacity();
        if (size_write == 0)
            break;
        Segment seg = _substrs[_output.bytes_written()];
        _substrs.erase(_output.bytes_written());
        if (size_write >= seg.data.size()) {
            // write all
            _output.write(seg.data);
            if (seg.eof)
                _output.end_input();
            size_write = seg.data.size();
        } else {
            // write part
            _output.write(seg.data.substr(0, size_write));
            // change the seg
            seg.index = seg.index + size_write;
            seg.data = seg.data.substr(size_write);
            _substrs[seg.index] = seg;
        }
        _substrs_size -= size_write;
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _substrs_size; }

bool StreamReassembler::empty() const { return _substrs.size() == 0; }
