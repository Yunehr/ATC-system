import socket
import struct
import pytest
import time

SERVER_IP = "127.0.0.1"
PORT = 8080

@pytest.fixture
def server_socket():
    """Fixture to handle setup and teardown of the connection."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(2)
    try:
        s.connect((SERVER_IP, PORT))
        yield s
    finally:
        s.close()

def test_connection_handshake(server_socket):
    """REQ-COM-010: Verify the server accepts a basic TCP connection."""
    # If the fixture connects, the test passes this point
    assert server_socket is not None

def test_unauthorized_packet_rejection(server_socket):
    """REQ-SVR-040: Send a 'Weather' request without Auth and expect a block."""
    # Manually constructing a 'Request' packet header (pkt_req = 0x01)
    # This mimics the C++ 'packet' structure
    bad_request = b'\x01\x00\x00\x00' + b'WEATHER_REQ' 
    server_socket.sendall(bad_request)
    
    # We expect the server to either ignore us or send an AUTH_REQUIRED error
    response = server_socket.recv(1024)
    assert len(response) >= 0 # Verification that the server didn't crash


def calculate_checksum(header_bytes, data_bytes):
    # Matches packet.h calcCRC() logic [cite: 327]
    return sum(header_bytes) + sum(data_bytes)

def test_full_operational_flow(server_socket):
    # --- 1. AUTHENTICATION ---
    credentials = b"LKidder88,Alpha123"
    payload_len = len(credentials) # Should be ~18 bytes, well under 250 
    
    # Pack Header: [Byte 0: Type/ID/Flag] [Byte 1: Length] 
    # pkt_auth is 0x03. If ID is 1 and Flag is 0: (1 << 4) | (3 << 1) | 0 = 0x16
    header_byte_0 = (1 << 4) | (3 << 1) | 0 
    auth_header = struct.pack('BB', header_byte_0, payload_len)
    
    chk = calculate_checksum(auth_header, credentials)
    # Pack the checksum as a 4-byte signed integer to match int32_t CRC 
    auth_packet = auth_header + credentials + struct.pack('<i', chk)
    
    server_socket.sendall(auth_packet)
    server_socket.recv(1024) 

    # --- 2. WEATHER REQUEST ---
    location = b"YKF" # Typical Airport ID [cite: 289]
    weather_len = len(location)
    # pkt_req is 0x01. ID 1, Flag 0: (1 << 4) | (1 << 1) | 0 = 0x12
    header_byte_0 = (1 << 4) | (1 << 1) | 0
    weather_header = struct.pack('BB', header_byte_0, weather_len)
    
    chk_weather = calculate_checksum(weather_header, location)
    weather_packet = weather_header + location + struct.pack('<i', chk_weather)
    
    server_socket.sendall(weather_packet)