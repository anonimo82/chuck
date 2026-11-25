# MediaTransport — packet framing v1

Transport-independent binary envelope; no sockets, sessions, encryption, authentication, fragmentation, retransmission or media codec negotiation yet. Do not treat CRC as authentication or expose this to untrusted network endpoints. No hard-realtime guarantee; byte arrays are copied and CRC calculated synchronously.

All multi-byte fields are unsigned big-endian. Timestamp is caller-supplied microseconds relative to a caller-defined origin, restricted to signed 64-bit range. Stream IDs are nonzero; sequences are unsigned 32-bit, with ordering policy deferred. Type IDs: 1 raster, 2 vector, 3 model, 4 waveform, 5 MIDI, 6 video, 7 text. Payload bytes are opaque (including empty payloads); validity of actual media is not implied.

| Offset | Bytes | Field |
|---|---:|---|
| 0 | 4 | CKMP magic |
| 4 | 1 | Version 1 |
| 5 | 1 | Type |
| 6 | 2 | Reserved, zero |
| 8 | 4 | Stream |
| 12 | 4 | Sequence |
| 16 | 8 | Timestamp |
| 24 | 4 | Payload length (max 1 MiB) |
| 28 | 4 | CRC-32/ISO-HDLC over bytes 0–27 followed by payload |
| 32 | variable | Payload |

ChucK: import MediaTransport. Static methods headerSize(), maxPayload(), encode(type,stream,sequence,timestampUs,payload,packet), decode(packet,metadata,payload). Input and output int arrays represent bytes 0–255; output packet must have exactly 32+payload.size() elements. Decode requires metadata of exactly four elements (type, stream, sequence, timestampUs), and output payload of exactly the encoded length. Metadata and payload must be separate arrays. Returns 1 on success, 0 on rejection; validation precedes output writes. Requires a ChucK build with 64-bit integers to use full field ranges. No change to existing media APIs.

## Local TCP transport (PR #196)

MediaLoopback is a single-peer, IPv4 loopback-only TCP endpoint. It binds exclusively to 127.0.0.1; no remote addresses, UDP, encryption or authentication. A local process can still connect, so use only in a trusted local environment.

- listen(port): 1 on success; 0 chooses an ephemeral port. port() reports the listener port, or 0. accept(): 1 accepted, 0 would block, -1 invalid state/error.
- connect(port): 1 on success, 0 on failure. Local handshake may block; subsequent socket I/O is nonblocking.
- sendPacket(packet): enqueue one complete validated CKMP packet, 1 on success; 0 when busy, invalid or disconnected. sendPending() indicates unsent bytes, not remote acknowledgement.
- poll(): advance at most 64 KiB of send and 64 KiB of receive per call; 1 progress, 0 idle, -1 closed/error. CRC verification is synchronous. Caller must poll both endpoints and avoid audio-critical shreds.
- packetSize(): 0 until a complete validated packet is ready. receivePacket(out): 1 on success, 0 if unavailable or wrong size. One packet of receive backpressure; no silent overwrites.
- connected(): local socket state (not a remote liveness guarantee). disconnect(): drops peer and pending data but keeps listener. close(): also closes listener. Malformed framing, CRC failures and EOF close peer and discard pending data. No automatic reconnection.

TCP ordering and partial reads/writes handled internally. Packet payload limit remains 1 MiB; one queued send and one assembled receive per endpoint. No hard-realtime guarantee or delivery acknowledgement. Windows Winsock and POSIX sockets; native validation pending.
