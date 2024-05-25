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
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    // Your code here.
    uint32_t mask = 0xFFFFFFFFU;
    // 为啥要区分prefix_length为0，因为(mask >> 32的结果为mask而不是0)
    if (prefix_length)
        mask = (mask >> (32 - prefix_length)) << (32 - prefix_length);
    else
        mask = 0;
    RouteEntry entry{route_prefix, mask, next_hop, interface_num};
    _route_table.push_back(entry);
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    // Your code here.
    uint32_t dst = dgram.header().dst;
    uint32_t now_mask = 0;
    ssize_t match_idx = -1;

    for (size_t i = 0; i < _route_table.size(); ++ i) {
        auto &entry = _route_table.at(i);
        if ((dst & entry.mask) == entry.dst && entry.mask >= now_mask) {
            match_idx = i;
            now_mask = entry.mask;   
        }
    }
    
    // 很坑的bug：这里必须是ttl --,不能是 --ttl,因为如果ttl为0，那么先减就溢出了
    if (~match_idx && (dgram.header().ttl -- > 1)) {
        auto &entry = _route_table.at(match_idx);
        Address next_hop = entry.gateway.has_value() ? entry.gateway.value() : Address::from_ipv4_numeric(dst);
        interface(entry.iface).send_datagram(dgram, next_hop);
    }
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
