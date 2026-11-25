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

## Negotiated sessions (PR #197)

MediaSession wraps MediaLoopback without changing its API or the CKMP v1 envelope. Both peers send a 12-byte CKHS hello using type 7, reserved stream 0xffffffff, zero sequence and timestamp. Hello payload: magic CKHS, version 1, message kind 1, seven-bit media mask, zero reserved byte, big-endian 32-bit maximum payload. Both peers must support version 1 and at least one common media type. The effective mask is the intersection; the effective payload limit is the smaller advertised limit. Type bits 0–6 correspond to media IDs 1–7. Default mask 127 and limit 1048576. Reserved stream is unavailable to applications using MediaSession; raw MediaLoopback remains unchanged.

Same endpoint methods as MediaLoopback, except connected() means negotiation complete. configure(mask,limit) is allowed only without a connected peer. state(): 0 idle, 1 negotiating, 2 ready, 3 failed. error(): 0 none, 1 transport/internal failure, 2 invalid hello/version, 3 no common media, 4 negotiation timeout, 5 incoming policy violation. mediaMask() and payloadLimit() report negotiated values (zero on idle/failure; may become available during negotiation). poll() must be called to advance negotiation, with a five-second monotonic deadline checked cooperatively. No application sends before readiness; incoming frames are checked against negotiated policy before delivery. Disconnect/close clear state; listeners survive peer failure and can accept again. This is compatibility negotiation, not authentication, identity verification or encryption. Loopback only; no remote endpoints or hard-realtime guarantee.

## Explicit IPv4 endpoints (PR #198)

Both MediaLoopback (historical class name) and MediaSession support allowRemote(0 or 1), remoteEnabled(), listenAt(address,port,allowedPeer), connectAt(address,port,timeoutMs), and peerAddress(). Remote access is disabled by default and can only be changed while neither listening nor connected. Explicit numeric IPv4 unicast addresses only; wildcard, hostname, multicast and addresses ending in .255 are rejected conservatively (not a full subnet validation). A listener binds to one explicit interface and accepts only the exact configured source IPv4 address. A rejected source connection is closed and accept returns 0. This is an IP filter, not authentication. Existing listen(port) and connect(port) remain loopback-only, with a 1000 ms connect timeout; explicit connect timeout 1–5000 ms uses nonblocking socket plus select and may block the calling shred until completion. peerAddress() is empty without a peer. Remote opt-in persists across close; disable explicitly when no longer needed. Use only trusted private networks with firewall restrictions. No encryption, authentication, DNS, IPv6, NAT traversal or hard-realtime guarantee. Real cross-host operation requires separate validation. Tests use loopback and reject unauthorized source addresses without external network traffic.

## Transport diagnostics and integration (PR #199)

Both endpoint classes expose sentPackets(), receivedPackets(), sentBytes(), receivedBytes(), transportError(). Counters describe the current or most recent accepted/connected peer; reset on successful new peer establishment and saturate at INT64_MAX. Packet counts include validated CKMP envelopes, including session hello; byte counts include partial socket I/O and headers. A sent packet is not a remote acknowledgement. Received packet count increments at complete framing validation, before session policy checks or application consumption. transportError(): 0 no recorded I/O error, 1 EOF, 2 socket error, 3 malformed frame, 4 internal exception. Session policy errors remain separate via error(). Explicit close/disconnect preserve counters for inspection. Queued validated packets are not repeatedly decoded during idle polling. No automatic reconnection, authentication, encryption or hard-realtime guarantees.

## Waveform PCM packets (PR #201)

MediaPCM encodes CKMP type 4 packets with a 16-byte CKAP v1 payload header followed by interleaved signed 16-bit PCM samples, big-endian. Header offsets: 0–3 CKAP, 4 version 1, 5 format 1 (PCM16), 6 channels, 7 reserved zero, 8–11 sample rate, 12–15 frame count. Rates 8000–192000 Hz, channels 1–8, positive frame count. Total payload must fit the existing 1 MiB limit. Stream 0 and the session control stream are forbidden. Samples are finite floats in [-1,1], mapped by rounding with scales 32768 for negative and 32767 for nonnegative values; decoded with the corresponding inverse scale. Quantization is lossy, not float-bit-exact. Caller supplies timestamps and interleaved channels. API: packetSize(rate,channels,frames) returns exact full CKMP size or 0; encode(stream,sequence,timestampUs,rate,channels,samples,packet), inspect(packet,metadata[6]), decode(packet,samples). Metadata is stream, sequence, timestamp, rate, channels, frames. All output arrays must have exact sizes. Returns 1 on success, 0 on rejection. Use existing MediaSession.sendPacket/receivePacket; negotiated payload limits still apply. No scheduling, resampling, jitter buffer, playback, live capture integration, or automatic chunking yet.

## Sequential PCM blocks (PR #202)

MediaPCMStream tracks one direction of one stream. configure(stream,rate,channels,originUs,firstSequence) initializes the sample timeline; clear() resets it. encodeBlock(samples,packet) emits CKAP v1 and advances only on successful encoding, not delivery. Retain and retry an encoded packet if MediaSession.sendPacket rejects it; do not encode the next block until the prior packet is enqueued. decodeBlock(packet,samples) requires matching stream, rate, channels, sequence, timestamp and exact output size. Timestamp is originUs + floor(cumulative frames * 1000000 / rate), calculated with overflow checks and no per-block rounding drift. Variable block lengths are supported. No sequence wrap: after sequence 4294967295, the next sequence is exhausted; reconfigure explicitly. Diagnostics: configured(), rate(), channels(), frames(), blocks(), nextSequence(), nextTimestamp(), error(), packetSize(frames). Error codes: 0 success/reset, 1 unconfigured, 2 invalid packet/samples/output, 3 stream/format mismatch, 4 unexpected sequence, 5 unexpected timestamp, 6 timeline exhausted. Rejected blocks do not advance timeline. No gap repair, retransmission, automatic scheduling, buffering or hardware clock correction; separate objects for transmit and receive. The existing MediaSession APIs remain unchanged.
