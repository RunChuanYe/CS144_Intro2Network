#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <iostream>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _outstanding_segs()
    , _ackno(0)
    , _rx_time_left(_initial_retransmission_timeout)
    , _zero_seg() {}

uint64_t TCPSender::bytes_in_flight() const {
    // return the total size of the _outstanding_segs's space
    uint64_t total_flight = 0;
    for (auto it = _outstanding_segs.begin(); it != _outstanding_segs.end(); ++it) {
        total_flight += it->second.length_in_sequence_space();
    }
    return total_flight;
}

void TCPSender::fill_window() {
    if (next_seqno_absolute() == 0) {
        // first send, send the empty segment with the SYN
        TCPSegment seg;
        seg.header().syn = true;
        seg.header().seqno = _isn;
        _segments_out.push(seg);
        _outstanding_segs[_next_seqno] = seg;
        // more reasonable
        _next_seqno += seg.length_in_sequence_space();

        // outstanding is not empty!
        if (!timer_running) {
            timer_running = true;
            _rx_time_left = _initial_retransmission_timeout;
        }

    } else if (_win_size > bytes_in_flight() && !_zero_seg_send) {
        // only fin, and not send already send
        if (stream_in().eof() && next_seqno_absolute() < stream_in().bytes_written() + 2) {
            TCPSegment seg;
            seg.header().seqno = next_seqno();
            seg.header().fin = true;

            _segments_out.push(seg);
            _outstanding_segs[_next_seqno] = seg;

            _next_seqno += seg.length_in_sequence_space();

            if (!timer_running) {
                timer_running = true;
                _rx_time_left = _initial_retransmission_timeout;
            }
        }

        // send the data first then fin
        while (!stream_in().buffer_empty() && _win_size > bytes_in_flight()) {
            // fill the win_size first, only when the win_size not 0, and the stream has bytes left
            ::uint16_t transfer_size = (_win_size - bytes_in_flight()) < TCPConfig::MAX_PAYLOAD_SIZE
                                           ? (_win_size - bytes_in_flight())
                                           : TCPConfig::MAX_PAYLOAD_SIZE;
            Buffer s = stream_in().read(transfer_size);

            TCPSegment seg;
            seg.header().seqno = next_seqno();
            seg.payload() = Buffer(s);

            // check if it has space and stream is ended and empty, then add the fin
            if ((_win_size > (bytes_in_flight() + seg.length_in_sequence_space())) && stream_in().eof()) {
                // add the fin
                seg.header().fin = true;
            }

            _segments_out.push(seg);
            _outstanding_segs[_next_seqno] = seg;
            _next_seqno += seg.length_in_sequence_space();

            if (!timer_running) {
                timer_running = true;
                _rx_time_left = _initial_retransmission_timeout;
            }
        }

    } else if (_win_size == 0 && (next_seqno_absolute() > bytes_in_flight())) {
        // _win_size < bytes_in_flight()

        // already ack then send empty to test

        // only when the eof not set send empty to test

        // the FIN need to be ack by receiver

        // send as the _win_size is 1, for the continuous send

        // send a byte
        if (!stream_in().buffer_empty() && !_zero_seg_send) {
            _zero_seg_send = true;

            _zero_seg.header().seqno = next_seqno();
            _zero_seg.payload() = stream_in().read(1);
            _zero_seg_seq = next_seqno_absolute();

            _next_seqno += _zero_seg.length_in_sequence_space();
            _segments_out.push(_zero_seg);
            if (!timer_running) {
                timer_running = true;
                _rx_time_left = _initial_retransmission_timeout;
            }
        }
        cerr << "send eof zero test before, " << endl;
        if (stream_in().eof() && !_zero_seg_send) {
            _zero_seg_send = true;

            _zero_seg.header().seqno = next_seqno();
            _zero_seg.header().fin = true;
            // set empty payload
            _zero_seg.payload() = Buffer("");
            _zero_seg_seq = next_seqno_absolute();

            _next_seqno += _zero_seg.length_in_sequence_space();
            _segments_out.push(_zero_seg);
            if (!timer_running) {
                timer_running = true;
                _rx_time_left = _initial_retransmission_timeout;
            }

            cerr << "send eof zero test after, " << endl;
            cerr << "timer_running: " << timer_running << endl;
            cerr << "time_left: " << _initial_retransmission_timeout << endl;
            cerr << "rx_time_left: " << _rx_time_left << endl;

            TCPSegment seg = _zero_seg;
            std::cerr << "(" << (seg.header().ack ? "A=1," : "A=0,") << (seg.header().rst ? "R=1," : "R=0,")
                      << (seg.header().syn ? "S=1," : "S=0,") << (seg.header().fin ? "F=1," : "F=0,")
                      << "ackno=" << seg.header().ackno << ","
                      << "win=" << seg.header().win << ","
                      << "seqno=" << seg.header().seqno << ","
                      << "payload_size=" << seg.payload().size() << ","
                      << "data=" << seg.payload().copy() << std::endl;
        }
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // look up all the collection segment has been sent
    // remove any that have been fully acknowledged!

    // the ack_absoluate_seqno must less than the _next_seqno
    std::uint64_t ack_absoluate_seqno = unwrap(ackno, _isn, _next_seqno);
    if (ack_absoluate_seqno > _next_seqno) {
        if (ack_absoluate_seqno < (1ul << 32))
            // impossible and invalid ackno!
            return;
        else
            ack_absoluate_seqno -= (1ul << 32);
    }

    if (ack_absoluate_seqno > _zero_seg_seq) {
        _zero_seg_send = false;
    }

    for (auto it = _outstanding_segs.begin(); it != _outstanding_segs.end();) {
        // all ack, remove from the outstanding_segs
        if ((it->second.length_in_sequence_space() + it->first - 1) < ack_absoluate_seqno) {
            it = _outstanding_segs.erase(it);
        } else if (it->first >= ack_absoluate_seqno) {
            // reg left are all not acked!
            break;
        } else
            it++;
    }

    if (_outstanding_segs.empty() && !_zero_seg_send)
        timer_running = false;

    // check whether is the newest ack, to reset the time_out
    if (_ackno != ackno) {
        _initial_retransmission_timeout >>= _rx_times;
        _rx_times = 0;
        // reset the timer
        _rx_time_left = _initial_retransmission_timeout;
    }

    // keep the win_size, ackno for fill_windows
    _win_size = window_size;
    _ackno = ackno;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // check whether the _rx_time_left is expire or not

    if (_zero_seg_send) {
        // only for the _zero_seg
        _rx_time_left = _rx_time_left > ms_since_last_tick ? _rx_time_left - ms_since_last_tick : 0;
        if (!_rx_time_left) {
            // re send
            segments_out().push(_zero_seg);
            // reset time!
            timer_running = true;
            _rx_time_left = _initial_retransmission_timeout;
        }
        return;
    }

    // not any retx needed
    if (_outstanding_segs.empty() || !timer_running) {
        timer_running = false;
        return;
    }

    _rx_time_left = _rx_time_left > ms_since_last_tick ? _rx_time_left - ms_since_last_tick : 0;
    if (!_rx_time_left) {
        // _win_size not zero, or is the first time to transmit (SYN)
        // send old seg, so not need the _win_size > bytes_in_flight()
        if (_win_size || (next_seqno_absolute() > 0 && next_seqno_absolute() == bytes_in_flight())) {
            _rx_times++;
            _initial_retransmission_timeout *= 2;
        }
        // need to retransmit
        // just send the first seg in the map, for the seg in the map
        // is order by seq, that is also the time it send!
        segments_out().push(_outstanding_segs.begin()->second);
        // reset time!
        timer_running = true;
        _rx_time_left = _initial_retransmission_timeout;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _rx_times; }

void TCPSender::send_empty_segment() {
    // produce an empty seg
    TCPSegment seg;
    seg.header().seqno = next_seqno();
    _segments_out.push(seg);
}
