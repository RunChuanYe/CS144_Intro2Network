#include "tcp_receiver.hh"

#include <iostream>
// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    // if SYN is set, keep the _isn
    ::uint32_t data_seqno;
    data_seqno = seg.header().seqno.raw_value();
    if (seg.header().syn) {
        _isn = seg.header().seqno;
        _isn_set = true;
        // data start from data_seqno + 1
        data_seqno = _isn.raw_value() + 1;
    }

    // if FIN is set, mark the end of entire byte stream
    // seqno -> absolute seqno -> index
    // only when the isn is set, can we push the data to reassembler
    if (_isn_set) {
        if (seg.header().fin) {
            _reassembler.push_substring(
                seg.payload().copy(), unwrap(WrappingInt32(data_seqno), _isn, stream_out().bytes_written()) - 1, true);
        } else
            _reassembler.push_substring(
                seg.payload().copy(), unwrap(WrappingInt32(data_seqno), _isn, stream_out().bytes_written()) - 1, false);
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    // If the ISN hasn’t been set yet, return an empty optional.
    if (!_isn_set) {
        return {};
    } else if (stream_out().input_ended()) {
        // include the FIN byte! so + 1 again
        // index -> absolute seqno (index + 1) -> seqno
        return wrap(stream_out().bytes_written() + 2, _isn);
    } else {
        // index -> absolute seqno (index + 1) -> seqno
        return wrap(stream_out().bytes_written() + 1, _isn);
    }
}

size_t TCPReceiver::window_size() const { return stream_out().remaining_capacity(); }
