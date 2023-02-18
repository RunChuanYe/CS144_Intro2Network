#include "byte_stream.hh"

#include <iostream>
// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity)
    : _capacity(capacity), _written(0), _read(0), _buffer(""), _input_end(false) {}

size_t ByteStream::write(const string &data) {
    size_t size_written = remaining_capacity();
    size_written = min(size_written, data.size());
    _buffer += data.substr(0, size_written);

    _written += size_written;
    return size_written;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const { return _buffer.substr(0, min(len, _buffer.size())); }

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t len_read = min(len, _buffer.size());
    _buffer = _buffer.substr(len_read, _buffer.size() - len_read);
    _read += len_read;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string res = peek_output(len);
    pop_output(len);
    return res;
}

void ByteStream::end_input() { _input_end = true; }

bool ByteStream::input_ended() const { return _input_end; }

size_t ByteStream::buffer_size() const { return _buffer.size(); }

bool ByteStream::buffer_empty() const { return _buffer.size() == 0; }

bool ByteStream::eof() const {
    // not _read == _written
    // init: _read == _written == 0
    if (input_ended() && buffer_empty())
        return true;
    else
        return false;
}

size_t ByteStream::bytes_written() const { return _written; }

size_t ByteStream::bytes_read() const { return _read; }

size_t ByteStream::remaining_capacity() const { return _capacity - _buffer.size(); }
