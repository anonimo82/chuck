#pragma once
// Include before chuck_dl.h on Windows.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#endif
#include "MediaFrame.h"
#include <algorithm>

namespace mediawire {
#ifdef _WIN32
using Socket=SOCKET;
constexpr Socket invalidSocket=INVALID_SOCKET;
inline void closeSocket(Socket s) { closesocket(s); }
inline bool retryable() { int e=WSAGetLastError(); return e==WSAEWOULDBLOCK||e==WSAEINTR; }
inline bool nonblocking(Socket s) { u_long yes=1; return ioctlsocket(s,FIONBIO,&yes)==0; }
using AddrLen=int;
#else
using Socket=int;
constexpr Socket invalidSocket=-1;
inline void closeSocket(Socket s) { close(s); }
inline bool retryable() { return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR; }
inline bool nonblocking(Socket s) { int f=fcntl(s,F_GETFL,0); return f>=0&&fcntl(s,F_SETFL,f|O_NONBLOCK)==0; }
using AddrLen=socklen_t;
#endif
inline sockaddr_in loopback(unsigned port) {
    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(static_cast<uint16_t>(port));
    a.sin_addr.s_addr=htonl(INADDR_LOOPBACK); return a;
}
inline bool preparePeer(Socket s) {
#ifdef SO_NOSIGPIPE
    int yes=1;
    if(setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,reinterpret_cast<const char*>(&yes),sizeof(yes))!=0) return false;
#endif
    return nonblocking(s);
}
class Loopback {
    Socket listener_=invalidSocket, peer_=invalidSocket;
    bool available_=true, ready_=false;
    unsigned port_=0;
    Bytes tx_,rx_;
    size_t sent_=0, target_=headerSize;
    void resetPeer() {
        if(peer_!=invalidSocket) closeSocket(peer_);
        peer_=invalidSocket; tx_.clear(); rx_.clear();
        sent_=0; target_=headerSize; ready_=false;
    }
    int fail() { resetPeer(); return -1; }
public:
    Loopback() {
#ifdef _WIN32
        WSADATA data{}; available_=WSAStartup(MAKEWORD(2,2),&data)==0;
#endif
    }
    Loopback(const Loopback&)=delete;
    Loopback& operator=(const Loopback&)=delete;
    ~Loopback() {
        stop();
#ifdef _WIN32
        if(available_) WSACleanup();
#endif
    }
    bool connected() const { return peer_!=invalidSocket; }
    unsigned port() const { return port_; }
    bool busy() const { return !tx_.empty(); }
    size_t packetSize() const { return ready_?rx_.size():0; }
    void disconnect() { resetPeer(); }
    void stop() {
        resetPeer();
        if(listener_!=invalidSocket) closeSocket(listener_);
        listener_=invalidSocket; port_=0;
    }
    bool listenOn(unsigned port) {
        if(!available_||listener_!=invalidSocket||connected()||port>65535) return false;
        Socket s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(s==invalidSocket) return false;
        auto a=loopback(port); AddrLen n=sizeof(a);
        if(bind(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0||
           listen(s,1)!=0||!nonblocking(s)||
           getsockname(s,reinterpret_cast<sockaddr*>(&a),&n)!=0) {
            closeSocket(s); return false;
        }
        listener_=s; port_=ntohs(a.sin_port); return true;
    }
    int acceptOne() {
        if(listener_==invalidSocket||connected()) return -1;
        Socket s=accept(listener_,nullptr,nullptr);
        if(s==invalidSocket) return retryable()?0:-1;
        if(!preparePeer(s)) { closeSocket(s); return -1; }
        peer_=s; return 1;
    }
    bool connectTo(unsigned port) {
        if(!available_||listener_!=invalidSocket||connected()||port==0||port>65535) return false;
        Socket s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(s==invalidSocket) return false;
        auto a=loopback(port);
        // Local TCP handshake only. This call may block in the OS.
        if(connect(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0||!preparePeer(s)) {
            closeSocket(s); return false;
        }
        peer_=s; return true;
    }
    bool enqueue(Bytes packet) {
        Header h; Bytes payload;
        if(!connected()||busy()||!decode(packet,h,payload)) return false;
        tx_.swap(packet); sent_=0; return true;
    }
    const Bytes &front() const { return rx_; }
    bool take(Bytes &packet) {
        if(!ready_) return false;
        packet.swap(rx_); rx_.clear(); target_=headerSize; ready_=false; return true;
    }
    int poll() {
        if(!connected()) return -1;
        bool progress=false;
        try {
            if(busy()) {
                size_t n=std::min<size_t>(65536,tx_.size()-sent_);
#ifdef MSG_NOSIGNAL
                constexpr int flags=MSG_NOSIGNAL;
#else
                constexpr int flags=0;
#endif
                auto r=send(peer_,reinterpret_cast<const char*>(tx_.data()+sent_),static_cast<int>(n),flags);
                if(r>0) {
                    sent_+=static_cast<size_t>(r); progress=true;
                    if(sent_==tx_.size()) { tx_.clear(); sent_=0; }
                } else if(r==0||!retryable()) return fail();
            }
            if(!ready_) {
                size_t old=rx_.size(),n=std::min<size_t>(65536,target_-old);
                rx_.resize(old+n);
                auto r=recv(peer_,reinterpret_cast<char*>(rx_.data()+old),static_cast<int>(n),0);
                if(r<=0) {
                    rx_.resize(old);
                    if(r==0||!retryable()) return fail();
                } else {
                    rx_.resize(old+static_cast<size_t>(r)); progress=true;
                    if(rx_.size()==target_) {
                        if(target_==headerSize) {
                            if(rx_[0]!='C'||rx_[1]!='K'||rx_[2]!='M'||rx_[3]!='P'||
                               rx_[4]!=1||rx_[5]<1||rx_[5]>7||rx_[6]||rx_[7]||
                               !get(rx_,8,4)||get(rx_,16,8)>0x7fffffffffffffffull||
                               get(rx_,24,4)>maxPayload) return fail();
                            target_=headerSize+static_cast<size_t>(get(rx_,24,4));
                        }
                        if(rx_.size()==target_) {
                            Header h; Bytes payload;
                            if(!decode(rx_,h,payload)) return fail();
                            ready_=true;
                        }
                    }
                }
            }
            return progress?1:0;
        } catch(...) { return fail(); }
    }
};
}
