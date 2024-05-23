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
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
// TODO: 不发送5s以内的重复的ARP报文
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    if (_arp_table.count(next_hop_ip)) {
        // 重置过期时间
        _arp_table[next_hop_ip].time_pass = 0;

        // 包装并发送数据即可
        EthernetFrame ethernet_frame = EthernetFrame();
        ethernet_frame.header().dst = _arp_table[next_hop_ip].mac_address;
        ethernet_frame.header().src = _ethernet_address;
        ethernet_frame.header().type = EthernetHeader::TYPE_IPv4;
        ethernet_frame.payload().append(dgram.serialize());
        _frames_out.push(ethernet_frame);
    } else {

        // 将数据报暂存
        if (_waiting_datagram.count(next_hop_ip)) {
            _waiting_datagram[next_hop_ip].push_back(dgram);
        } else {
            _waiting_datagram.insert(make_pair(next_hop_ip, vector<InternetDatagram>{dgram}));
        }

        //如果5s内没有发送过ARP泛洪 -> 发送ARP Request获取mac地址
        if (!_send_table.count(next_hop_ip)) {
            EthernetFrame ethernet_frame = EthernetFrame();
            ethernet_frame.header().dst = ETHERNET_BROADCAST;
            ethernet_frame.header().src = _ethernet_address;
            ethernet_frame.header().type = EthernetHeader::TYPE_IPv4;

            ARPMessage arp_message = ARPMessage();
            arp_message.opcode = ARPMessage::OPCODE_REQUEST;
            arp_message.sender_ethernet_address = _ethernet_address;
            arp_message.sender_ip_address = _ip_address.ipv4_numeric();
            arp_message.target_ethernet_address = ETHERNET_BROADCAST;
            ethernet_frame.payload() = arp_message.serialize();
            _frames_out.push(ethernet_frame);
        }
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    EthernetHeader const &header = frame.header();
    if (header.dst != _ethernet_address || header.dst != ETHERNET_BROADCAST)
        return;
    
    if (header.type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram ipv4_datagram;
        if (ipv4_datagram.parse(frame.payload()) == ParseResult::NoError) {
            return optional<InternetDatagram>(ipv4_datagram);
        }
    } else if (header.type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp_message;
        if (arp_message.parse(frame.payload()) == ParseResult::NoError) {
            // 加入arp表
            _arp_table.insert(make_pair(arp_message.sender_ip_address, NetworkInterface::ARPEntry{0, arp_message.sender_ethernet_address}));

            if (arp_message.opcode == ARPMessage::OPCODE_REQUEST) {
                // 回复arp报文
                uint32_t local_ipv4_num = _ip_address.ipv4_numeric();
                if (arp_message.target_ip_address == local_ipv4_num && arp_message.target_ethernet_address == _ethernet_address) {
                    EthernetFrame ethernet_frame = EthernetFrame();
                    ethernet_frame.header().dst = arp_message.sender_ethernet_address;
                    ethernet_frame.header().src = _ethernet_address;
                    ethernet_frame.header().type = EthernetHeader::TYPE_IPv4;

                    ARPMessage arp_reply;
                    arp_reply.opcode = ARPMessage::OPCODE_REPLY;
                    arp_reply.sender_ethernet_address = _ethernet_address;
                    arp_reply.sender_ip_address = local_ipv4_num;
                    arp_reply.target_ethernet_address = arp_message.sender_ethernet_address;
                    arp_reply.target_ip_address = arp_message.sender_ip_address;
                    ethernet_frame.payload() = arp_message.serialize();
                    _frames_out.push(ethernet_frame);
                }
            } else {
                // 一定是OPCODE_REPLY，不然parse会报错
                uint32_t local_ipv4_num = _ip_address.ipv4_numeric();
                if (arp_message.target_ip_address == local_ipv4_num && arp_message.target_ethernet_address == _ethernet_address) {
                    // 重发暂存的报文
                    if (_waiting_datagram.count(arp_message.sender_ip_address)) {
                        for (auto &data : _waiting_datagram[arp_message.sender_ip_address]) {
                            send_datagram(data, Address::from_ipv4_numeric(arp_message.sender_ip_address));
                        }
                    }
                    _waiting_datagram.erase(arp_message.sender_ip_address);
                }
            }
        }
    }
    return optional<InternetDatagram>();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    // arp table
    for (auto iter = _arp_table.begin(); iter != _arp_table.end(); /* no op */) {
        ARPEntry &entry = iter->second;
        entry.time_pass += ms_since_last_tick;
        if (entry.time_pass >= ARPTableTimeOut) {
            iter = _arp_table.erase(iter);
        } else {
            iter ++;
        }
    }

    // send table
    for (auto iter = _send_table.begin(); iter != _send_table.end(); /* no op */) {
        SendEntry &entry = iter->second;
        entry.time_pass += ms_since_last_tick;
        if (entry.time_pass >= SendTimeOut) {
            iter = _send_table.erase(iter);
        } else {
            iter ++;
        }
    }
}
