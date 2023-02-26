#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address)
    , _ip_address(ip_address)
    , _IP_MAC_cache()
    , _time_since_cache()
    , _waiting_list()
    , _time_since_req_send()
    , _reqs() {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    // find the MAC corresponding to the IP
    if (_IP_MAC_cache.find(next_hop_ip) == _IP_MAC_cache.end()) {
        if (_waiting_list.find(next_hop_ip) != _waiting_list.end()) {
            // send req again at least after 5 seconds!
            if (_time_since_req_send[next_hop_ip] >= MAX_REQ_TIME) {
                // re send the req
                _time_since_req_send[next_hop_ip] = 0;
                _frames_out.push(_reqs[next_hop_ip]);
            }
            return;
        }

        // waiting the get the MAC
        _waiting_list[next_hop_ip] = dgram;
        // send the ARP
        EthernetFrame arp_frame;
        arp_frame.header().dst = ETHERNET_BROADCAST;
        arp_frame.header().src = _ethernet_address;
        arp_frame.header().type = EthernetHeader::TYPE_ARP;

        ARPMessage arp_message;
        arp_message.opcode = ARPMessage::OPCODE_REQUEST;
        arp_message.sender_ethernet_address = _ethernet_address;
        arp_message.sender_ip_address = _ip_address.ipv4_numeric();
        arp_message.target_ip_address = next_hop_ip;
        // arp_message.target_ethernet_address not set!

        // serialize
        arp_frame.payload() = arp_message.serialize();
        // send
        _frames_out.push(arp_frame);
        // remember the time
        _time_since_req_send[next_hop_ip] = 0;
        // remember the req
        _reqs[next_hop_ip] = arp_frame;
    } else {
        // send the just send the datagram
        EthernetAddress dest_mac = _IP_MAC_cache[next_hop_ip];
        // do not update the time
        // _time_since_cache[next_hop_ip] = 0;

        // send the IP datagram
        EthernetFrame ip_frame;
        ip_frame.header().dst = dest_mac;
        ip_frame.header().src = _ethernet_address;
        ip_frame.header().type = EthernetHeader::TYPE_IPv4;

        ip_frame.payload() = dgram.serialize();

        // send
        _frames_out.push(ip_frame);
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // ignore
    if (frame.header().dst != ETHERNET_BROADCAST && frame.header().dst != _ethernet_address)
        return {};

    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        // just return the IP datagram
        IPv4Datagram datagram;
        // just the first Buffer is the datagram
        if (datagram.parse(frame.payload().buffers().front()) == ParseResult::NoError)
            return datagram;
        return {};
    }

    if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp_message;
        // parse error
        if (arp_message.parse(frame.payload().buffers().front()) != ParseResult::NoError)
            return {};
        if (arp_message.opcode == ARPMessage::OPCODE_REPLY) {
            // a reply
            if (_waiting_list.find(arp_message.sender_ip_address) != _waiting_list.end()) {
                // remove the datagram and send
                InternetDatagram datagram = _waiting_list[arp_message.sender_ip_address];
                _waiting_list.erase(arp_message.sender_ip_address);
                // remove the time
                _time_since_req_send.erase(arp_message.sender_ip_address);
                // remove the req frame
                _reqs.erase(arp_message.sender_ip_address);

                // set the ip frame
                EthernetFrame ip_frame;
                ip_frame.header().dst = arp_message.sender_ethernet_address;
                ip_frame.header().src = _ethernet_address;
                ip_frame.header().type = EthernetHeader::TYPE_IPv4;

                ip_frame.payload() = datagram.serialize();
                // send
                _frames_out.push(ip_frame);
            }
        } else {
            // an arp request
            // send the reply if the request is our ip
            if (arp_message.target_ip_address == _ip_address.ipv4_numeric()) {
                EthernetFrame reply_frame;
                reply_frame.header().src = _ethernet_address;
                reply_frame.header().dst = frame.header().src;
                reply_frame.header().type = EthernetHeader::TYPE_ARP;
                // add the arp message
                ARPMessage arp_message_reply;
                arp_message_reply.opcode = ARPMessage::OPCODE_REPLY;
                arp_message_reply.target_ip_address = arp_message.sender_ip_address;
                arp_message_reply.sender_ip_address = _ip_address.ipv4_numeric();
                arp_message_reply.target_ethernet_address = arp_message.sender_ethernet_address;
                arp_message_reply.sender_ethernet_address = _ethernet_address;

                // serialize
                reply_frame.payload() = arp_message_reply.serialize();
                // send
                _frames_out.push(reply_frame);
            }
        }
        // update the ip mac cache
        _IP_MAC_cache[arp_message.sender_ip_address] = arp_message.sender_ethernet_address;
        // update the cache time
        _time_since_cache[arp_message.sender_ip_address] = 0;
    }

    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    // add the time_since_req_send
    for (auto it = _time_since_req_send.begin(); it != _time_since_req_send.end(); ++it) {
        it->second += ms_since_last_tick;
    }

    // add the time time_since_cache
    for (auto it = _time_since_cache.begin(); it != _time_since_cache.end();) {
        if (it->second + ms_since_last_tick >= MAX_CACHE_TIME) {
            // remove the cache entry
            _IP_MAC_cache.erase(it->first);
            it = _time_since_cache.erase(it);
        } else {
            it->second += ms_since_last_tick;
            ++it;
        }
    }
}
