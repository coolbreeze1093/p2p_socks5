#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SOCKS5 UDP ASSOCIATE + DNS relay test.

Works against any SOCKS5 endpoint of this project:
  - old client  (config.ini [socks5] bind_port, default 10801)
  - new rtcsocks CLI pair (default 10801, start both rtcsocks/rtcsocks-exit first)

Usage:
  python test/socket5_udp_test.py [socks5_port]

Steps:
  1. TCP control channel: method negotiation (NO AUTH) -> UDP ASSOCIATE
  2. Send a DNS A query for www.baidu.com to 114.114.114.114:53
     through the SOCKS5 UDP relay
  3. Verify the DNS response round-trips with a matching transaction id
"""

import random
import socket
import struct
import sys

SOCKS5_HOST = "127.0.0.1"
SOCKS5_PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 10801

DNS_SERVER = "114.114.114.114"
DNS_PORT = 53
DNS_DOMAIN = "www.baidu.com"

TCP_TIMEOUT = 5.0
UDP_TIMEOUT = 8.0


def socks5_udp_associate(socks_host, socks_port):
    """Open the TCP control channel and run greeting + UDP ASSOCIATE.

    Returns (tcp_sock, relay_host, relay_port).
    """
    tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp_sock.settimeout(TCP_TIMEOUT)
    tcp_sock.connect((socks_host, socks_port))

    # 1. greeting: VER=5, NMETHODS=1, METHOD=0 (NO AUTH)
    tcp_sock.sendall(b"\x05\x01\x00")
    resp = tcp_sock.recv(2)
    if resp != b"\x05\x00":
        raise RuntimeError(f"greeting failed: {resp.hex()}")

    print(f"[1] greeting ok: {resp.hex()}")

    # 2. UDP ASSOCIATE: VER=5, CMD=3, RSV=0, ATYP=1, 0.0.0.0:0
    req = b"\x05\x03\x00\x01" + socket.inet_aton("0.0.0.0") + struct.pack(">H", 0)
    tcp_sock.sendall(req)

    header = tcp_sock.recv(4)
    if len(header) < 4:
        raise RuntimeError(f"UDP ASSOCIATE reply too short: {header.hex()}")

    ver, rep, _rsv, atyp = header[0], header[1], header[2], header[3]
    if ver != 0x05:
        raise RuntimeError(f"bad reply version: 0x{ver:02x}")
    if rep != 0x00:
        raise RuntimeError(f"UDP ASSOCIATE failed, rep=0x{rep:02x}")

    # BND.ADDR / BND.PORT
    if atyp == 0x01:
        bnd_addr = socket.inet_ntoa(tcp_sock.recv(4))
    elif atyp == 0x03:
        n = tcp_sock.recv(1)[0]
        bnd_addr = tcp_sock.recv(n).decode()
    elif atyp == 0x04:
        bnd_addr = socket.inet_ntop(socket.AF_INET6, tcp_sock.recv(16))
    else:
        raise RuntimeError(f"unsupported ATYP in reply: 0x{atyp:02x}")

    bnd_port = struct.unpack(">H", tcp_sock.recv(2))[0]

    # RFC 1928: 0.0.0.0 means "use the address the request came from"
    if bnd_addr in ("0.0.0.0", "::"):
        bnd_addr = socks_host
        print(f"[2] relay is wildcard, using {bnd_addr}")

    print(f"[2] UDP relay: {bnd_addr}:{bnd_port}")
    return tcp_sock, bnd_addr, bnd_port


def build_dns_query(domain):
    """Build a standard DNS A query with a random transaction id."""
    transaction_id = random.randint(0, 0xFFFF)

    qname = b""
    for label in domain.strip(".").split("."):
        encoded = label.encode("ascii")
        qname += struct.pack("B", len(encoded)) + encoded
    qname += b"\x00"

    header = struct.pack(">HHHHHH", transaction_id, 0x0100, 1, 0, 0, 0)
    question = qname + struct.pack(">HH", 1, 1)  # QTYPE=A, QCLASS=IN

    return transaction_id, header + question


def send_dns_via_socks5(relay_host, relay_port, target_host, target_port, payload):
    """Send payload through the SOCKS5 UDP relay, return (addr, response payload)."""
    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_sock.settimeout(UDP_TIMEOUT)
    try:
        # SOCKS5 UDP header: RSV(2)=0, FRAG(1)=0, ATYP(1)=1, ADDR(4), PORT(2)
        header = b"\x00\x00\x00\x01" + socket.inet_aton(target_host) + struct.pack(">H", target_port)
        udp_sock.sendto(header + payload, (relay_host, relay_port))

        data, addr = udp_sock.recvfrom(4096)

        print(f"[4] reply from {addr[0]}:{addr[1]}, {len(data)} bytes")

        # strip the SOCKS5 UDP header (10 bytes for an IPv4 source)
        return addr, data[10:]
    finally:
        udp_sock.close()


def main():
    print("SOCKS5 UDP relay test")
    print(f"  endpoint : {SOCKS5_HOST}:{SOCKS5_PORT}")
    print(f"  dns      : {DNS_SERVER}:{DNS_PORT} ({DNS_DOMAIN})")
    print()

    tcp_sock, relay_host, relay_port = socks5_udp_associate(SOCKS5_HOST, SOCKS5_PORT)
    try:
        transaction_id, dns_query = build_dns_query(DNS_DOMAIN)
        print(f"[3] DNS query sent, tid=0x{transaction_id:04x}, {len(dns_query)} bytes")

        addr, dns_response = send_dns_via_socks5(
            relay_host, relay_port, DNS_SERVER, DNS_PORT, dns_query
        )

        if addr[0] != relay_host or addr[1] != relay_port:
            raise RuntimeError(f"reply from unexpected source {addr}")

        if len(dns_response) < 2:
            raise RuntimeError("DNS response too short")

        resp_tid = struct.unpack(">H", dns_response[:2])[0]
        if resp_tid != transaction_id:
            raise RuntimeError(
                f"DNS transaction id mismatch: sent 0x{transaction_id:04x}, got 0x{resp_tid:04x}"
            )

        rcode = dns_response[3] & 0x0F
        print(f"[5] DNS response ok, tid match, rcode={rcode}, {len(dns_response)} bytes")
        print("PASSED")
    finally:
        tcp_sock.close()


if __name__ == "__main__":
    try:
        main()
    except socket.timeout:
        print("FAILED: timeout (no response through the UDP relay)")
        print("  - is the P2P tunnel actually connected?")
        print("  - did UDP ASSOCIATE succeed on the exit side?")
        sys.exit(1)
    except Exception as e:
        print(f"FAILED: {type(e).__name__}: {e}")
        sys.exit(1)
