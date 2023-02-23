#ifndef SPONGE_LIBSPONGE_TCP_FACTORED_HH
#define SPONGE_LIBSPONGE_TCP_FACTORED_HH

#include "tcp_config.hh"
#include "tcp_receiver.hh"
#include "tcp_sender.hh"
#include "tcp_state.hh"

#include <iostream>

//! \brief A complete endpoint of a TCP connection
class TCPConnection {
  private:
    TCPConfig _cfg;
    TCPReceiver _receiver{_cfg.recv_capacity};
    TCPSender _sender{_cfg.send_capacity, _cfg.rt_timeout, _cfg.fixed_isn};

    //! outbound queue of segments that the TCPConnection wants sent
    std::queue<TCPSegment> _segments_out{};

    //! Should the TCPConnection stay active (and keep ACKing)
    //! for 10 * _cfg.rt_timeout milliseconds after both streams have ended,
    //! in case the remote TCPConnection doesn't know we've received its whole stream?
    bool _linger_after_streams_finish{true};

    size_t _time_since_last_seg_rec = 0;

    // the segment_out() must not be empty!
    void send_common_seg() {
        // fill the win and akcno fields in the seg
        TCPSegment seg = _sender.segments_out().front();
        _sender.segments_out().pop();

        if (_receiver.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
        }

        // send to IP
        _segments_out.push(seg);

        std::cerr << "(" << (seg.header().ack ? "A=1," : "A=0,") << (seg.header().rst ? "R=1," : "R=0,")
                  << (seg.header().syn ? "S=1," : "S=0,") << (seg.header().fin ? "F=1," : "F=0,")
                  << "ackno=" << seg.header().ackno << ","
                  << "win=" << seg.header().win << ","
                  << "seqno=" << seg.header().seqno << ","
                  << "payload_size=" << seg.payload().size() << "," << std::endl;
        //                  << "data=" << seg.payload().copy() << std::endl;
    }

    void send_all_segs() {
        // any time the _sender produce a seg, send them all
        while (!_sender.segments_out().empty()) {
            send_common_seg();
        }
    }

    void send_rst_seg() {
        // remote
        if (_sender.segments_out().empty()) {
            _sender.send_empty_segment();
        }
        TCPSegment seg = _sender.segments_out().front();
        seg.header().rst = true;
        if (_receiver.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
        }
        segments_out().push(seg);
    }

    bool time_wait_state() const {
        // waiting to make sure the ack for the fin is received by remote
        return _receiver.stream_out().eof() && _sender.stream_in().eof() && _sender.bytes_in_flight() == 0 &&
               (_sender.stream_in().bytes_written() + 2) == _sender.next_seqno_absolute() &&
               !_sender.get_zero_seg_send();
    }
    bool closing_state() const {
        // the same with the time wait state, but the fin is firstly received and then send to remote
        return time_wait_state();
    }

    bool connection_establish() {
        // SYN received ans SYN acked!
        return _receiver.ackno().has_value() && _sender.next_seqno_absolute() > _sender.bytes_in_flight();
    }

  public:
    //! \name "Input" interface for the writer
    //!@{

    //! \brief Initiate a connection by sending a SYN segment
    void connect();

    //! \brief Write data to the outbound byte stream, and send it over TCP if possible
    //! \returns the number of bytes from `data` that were actually written.
    size_t write(const std::string &data);

    //! \returns the number of `bytes` that can be written right now.
    size_t remaining_outbound_capacity() const;

    //! \brief Shut down the outbound byte stream (still allows reading incoming data)
    void end_input_stream();
    //!@}

    //! \name "Output" interface for the reader
    //!@{

    //! \brief The inbound byte stream received from the peer
    ByteStream &inbound_stream() { return _receiver.stream_out(); }
    //!@}

    //! \name Accessors used for testing

    //!@{
    //! \brief number of bytes sent and not yet acknowledged, counting SYN/FIN each as one byte
    size_t bytes_in_flight() const;
    //! \brief number of bytes not yet reassembled
    size_t unassembled_bytes() const;
    //! \brief Number of milliseconds since the last segment was received
    size_t time_since_last_segment_received() const;
    //!< \brief summarize the state of the sender, receiver, and the connection
    TCPState state() const { return {_sender, _receiver, active(), _linger_after_streams_finish}; };
    //!@}

    //! \name Methods for the owner or operating system to call
    //!@{

    //! Called when a new segment has been received from the network
    void segment_received(const TCPSegment &seg);

    //! Called periodically when time elapses
    void tick(const size_t ms_since_last_tick);

    //! \brief TCPSegments that the TCPConnection has enqueued for transmission.
    //! \note The owner or operating system will dequeue these and
    //! put each one into the payload of a lower-layer datagram (usually Internet datagrams (IP),
    //! but could also be user datagrams (UDP) or any other kind).
    std::queue<TCPSegment> &segments_out() { return _segments_out; }

    //! \brief Is the connection still alive in any way?
    //! \returns `true` if either stream is still running or if the TCPConnection is lingering
    //! after both streams have finished (e.g. to ACK retransmissions from the peer)
    bool active() const;
    //!@}

    //! Construct a new connection from a configuration
    explicit TCPConnection(const TCPConfig &cfg) : _cfg{cfg} {}

    //! \name construction and destruction
    //! moving is allowed; copying is disallowed; default construction not possible

    //!@{
    ~TCPConnection();  //!< destructor sends a RST if the connection is still open
    TCPConnection() = delete;
    TCPConnection(TCPConnection &&other) = default;
    TCPConnection &operator=(TCPConnection &&other) = default;
    TCPConnection(const TCPConnection &other) = delete;
    TCPConnection &operator=(const TCPConnection &other) = delete;
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_FACTORED_HH
