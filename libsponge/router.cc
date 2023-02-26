#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the _route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    // all need to do is just add the route entry
    RoutingTableEntry entry(route_prefix, prefix_length, next_hop, interface_num);
    _routing_table.push_back(entry);
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    // drop
    if (dgram.header().ttl <= 1)
        return;

    // complete the longest match algorithm
    RoutingTableEntry entry;
    bool found = false;
    uint32_t dest_ip = dgram.header().dst;
    for (size_t i = 0; i < _routing_table.size(); ++i) {
        if (match(dest_ip, _routing_table[i]._route_prefix, _routing_table[i]._prefix_length)) {
            if (!found) {
                found = true;
                entry = _routing_table[i];
            }
            if (entry._prefix_length < _routing_table[i]._prefix_length) {
                entry = _routing_table[i];
            }
        }
    }
    // drop
    if (!found)
        return;

    // decrement
    dgram.header().ttl--;

    if (entry._next_hop.has_value())
        // next hop not empty
        interface(entry._interface_num).send_datagram(dgram, entry._next_hop.value());
    else
        // next hop empty, get the dst
        interface(entry._interface_num).send_datagram(dgram, Address::from_ipv4_numeric(dgram.header().dst));
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}

bool Router::match(uint32_t dst, uint32_t entry_dst, uint8_t pre_len) {
    // just start loop from the most significant
    ::uint32_t mask = 1ul << 31;
    for (size_t i = 0; i < pre_len; ++i) {
        if ((dst & mask) != (entry_dst & mask)) {
            return false;
        }
        mask >>= 1;
    }
    return true;
}

Router::RoutingTableEntry::RoutingTableEntry(const uint32_t route_prefix,
                                             const uint8_t prefix_length,
                                             const optional<Address> next_hop,
                                             const size_t interface_num)
    : _route_prefix(route_prefix), _prefix_length(prefix_length), _next_hop(next_hop), _interface_num(interface_num) {}
