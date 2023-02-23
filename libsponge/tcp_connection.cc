#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_seg_rec; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!active()) {
        cerr << "active: false" << endl;
        return;
    }

    // if the RST is set
    if (seg.header().rst) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        // the active() is false.
        return;
    }

    // to the reader
    _receiver.segment_received(seg);
    // to the sender
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
        if (_sender.next_seqno_absolute() != 0) {
            // send only when the connection established
            _sender.fill_window();
        }
    }

    // set the time last rec
    if (time_wait_state()) {
        _time_since_last_seg_rec = 0;
    }

    // check in stream FIN or not
    if (_receiver.stream_out().input_ended() && !_sender.stream_in().input_ended()) {
        _linger_after_streams_finish = false;
    }

    // if the seg has at least one byte, then need to response
    // update the seqno and the win_size
    if (seg.length_in_sequence_space() && _receiver.ackno().has_value()) {
        // listening
        if (_sender.next_seqno_absolute() == 0) {
            _sender.fill_window();
        }
        if (_sender.segments_out().empty()) {
            // has seg?
            // get an empty seg
            _sender.send_empty_segment();
        }
        send_common_seg();
    }

    // send all other segs _sender want to send
    send_all_segs();

    // response to the keep-alive seg
    if (_receiver.ackno().has_value() && (seg.length_in_sequence_space() == 0) &&
        (seg.header().seqno == _receiver.ackno().value() - 1)) {
        _sender.send_empty_segment();
        send_common_seg();
    }
}

bool TCPConnection::active() const {
    if (_sender.stream_in().error() && _receiver.stream_out().error()) {
        // RST
        return false;
    }
    if (!_linger_after_streams_finish) {
        // no waiting
        // #1 #2 #3
        if (closing_state()) {
            return false;
        }
    } else {
        // need to waiting
        if (time_wait_state() && _time_since_last_seg_rec >= (10 * _cfg.rt_timeout)) {
            return false;
        }
    }
    return true;
}

size_t TCPConnection::write(const string &data) {
    // write data for sender to get
    size_t write_size = _sender.stream_in().write(data);
    // generate segs and send them all
    _sender.fill_window();
    send_all_segs();

    return write_size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    // tell the sender
    if (active()) {
        _sender.tick(ms_since_last_tick);
        // add the time
        if (time_wait_state())
            _time_since_last_seg_rec += ms_since_last_tick;

        // check REXT times
        if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
            // end the connection self
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();

            send_rst_seg();
        }

        if (active()) {
            // send all segs
            send_all_segs();
        }
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_all_segs();
}

void TCPConnection::connect() {
    // just call the sender.fill_windows is ok
    _sender.fill_window();
    // fetch the seg from _sender
    _segments_out.push(_sender.segments_out().front());
    _sender.segments_out().pop();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            // end the connection self
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
            send_rst_seg();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
