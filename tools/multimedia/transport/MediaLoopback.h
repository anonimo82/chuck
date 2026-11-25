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
#include <string>
#include <chrono>

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
inline bool parseEndpoint(const std::string &ip,unsigned port,sockaddr_in &a) {
    if(ip.empty()||ip.size()>15||port>65535||ip.find('\0')!=std::string::npos) return false;
    a={}; a.sin_family=AF_INET; a.sin_port=htons(static_cast<uint16_t>(port));
    if(inet_pton(AF_INET,ip.c_str(),&a.sin_addr)!=1) return false;
    uint32_t host=ntohl(a.sin_addr.s_addr);
    return (host>>24)!=0 && (host>>24)<224 && (host&0xffu)!=255;
}
inline bool isLocal(const sockaddr_in &a) { return (ntohl(a.sin_addr.s_addr)>>24)==127; }
inline bool connectPending() {
#ifdef _WIN32
    int e=WSAGetLastError(); return e==WSAEWOULDBLOCK||e==WSAEINPROGRESS;
#else
    return errno==EINPROGRESS||errno==EINTR;
#endif
}
inline bool timedConnect(Socket s,const sockaddr_in &a,unsigned timeoutMs) {
    if(!nonblocking(s)) return false;
#ifndef _WIN32
    if(s>=FD_SETSIZE) return false;
#endif
    if(connect(s,reinterpret_cast<const sockaddr*>(&a),sizeof(a))==0) return true;
    if(!connectPending()) return false;
    fd_set wr,err; FD_ZERO(&wr); FD_ZERO(&err); FD_SET(s,&wr); FD_SET(s,&err);
    timeval tv{}; tv.tv_sec=timeoutMs/1000; tv.tv_usec=(timeoutMs%1000)*1000;
#ifdef _WIN32
    int n=select(0,nullptr,&wr,&err,&tv);
#else
    int n=select(s+1,nullptr,&wr,&err,&tv);
#endif
    int e=0; AddrLen len=sizeof(e);
    return n>0&&getsockopt(s,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&e),&len)==0&&e==0;
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
    bool remote_=false;
    sockaddr_in allowed_{};
    std::string peerIp_;
    Bytes tx_,rx_;
    size_t sent_=0, target_=headerSize;
    uint64_t txPackets_=0,rxPackets_=0,txBytes_=0,rxBytes_=0;
    int lastError_=0;
    static void add(uint64_t &v,uint64_t n) { constexpr uint64_t cap=0x7fffffffffffffffull; v=n>cap-v?cap:v+n; }
    void resetStats() { txPackets_=rxPackets_=txBytes_=rxBytes_=0; lastError_=0; }
    void resetPeer() {
        if(peer_!=invalidSocket) closeSocket(peer_);
        peer_=invalidSocket; tx_.clear(); rx_.clear();
        sent_=0; target_=headerSize; ready_=false; peerIp_.clear();
    }
    int fail(int code) { resetPeer(); lastError_=code; return -1; }
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
    bool allowRemote(bool enabled) {
        if(listener_!=invalidSocket||connected()) return false;
        remote_=enabled; return true;
    }
    bool remoteEnabled() const { return remote_; }
    const std::string &peerAddress() const { return peerIp_; }
    bool connected() const { return peer_!=invalidSocket; }
    unsigned port() const { return port_; }
    uint64_t sentPackets() const { return txPackets_; }
    uint64_t receivedPackets() const { return rxPackets_; }
    uint64_t sentBytes() const { return txBytes_; }
    uint64_t receivedBytes() const { return rxBytes_; }
    int transportError() const { return lastError_; }
    bool busy() const { return !tx_.empty(); }
    size_t packetSize() const { return ready_?rx_.size():0; }
    void disconnect() { resetPeer(); }
    void stop() {
        resetPeer();
        if(listener_!=invalidSocket) closeSocket(listener_);
        listener_=invalidSocket; port_=0;
    }
    bool listenOn(unsigned port) { return listenAt("127.0.0.1",port,"127.0.0.1"); }
    bool listenAt(const std::string &ip,unsigned port,const std::string &peer) {
        sockaddr_in a{}, allow{};
        if(!available_||listener_!=invalidSocket||connected()||
           !parseEndpoint(ip,port,a)||!parseEndpoint(peer,0,allow)||
           (!remote_&&(!isLocal(a)||!isLocal(allow)))) return false;
        Socket s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(s==invalidSocket) return false;
        AddrLen n=sizeof(a);
        if(bind(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0||
           listen(s,1)!=0||!nonblocking(s)||
           getsockname(s,reinterpret_cast<sockaddr*>(&a),&n)!=0) {
            closeSocket(s); return false;
        }
        listener_=s; allowed_=allow; port_=ntohs(a.sin_port); return true;
    }
    int acceptOne() {
        if(listener_==invalidSocket||connected()) return -1;
        sockaddr_in peer{}; AddrLen len=sizeof(peer);
        Socket s=accept(listener_,reinterpret_cast<sockaddr*>(&peer),&len);
        if(s==invalidSocket) return retryable()?0:-1;
        if(peer.sin_addr.s_addr!=allowed_.sin_addr.s_addr) { closeSocket(s); return 0; }
        char ip[INET_ADDRSTRLEN]{};
        if(!preparePeer(s)||!inet_ntop(AF_INET,&peer.sin_addr,ip,sizeof(ip))) { closeSocket(s); return -1; }
        try { peerIp_=ip; } catch(...) { closeSocket(s); return -1; }
        resetStats(); peer_=s; return 1;
    }
    bool connectTo(unsigned port) { return connectAt("127.0.0.1",port,1000); }
    bool connectAt(const std::string &ip,unsigned port,unsigned timeoutMs) {
        sockaddr_in a{};
        if(!available_||listener_!=invalidSocket||connected()||!port||
           !timeoutMs||timeoutMs>5000||!parseEndpoint(ip,port,a)||(!remote_&&!isLocal(a))) return false;
        Socket s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(s==invalidSocket) return false;
        if(!timedConnect(s,a,timeoutMs)||!preparePeer(s)) { closeSocket(s); return false; }
        try { peerIp_=ip; } catch(...) { closeSocket(s); return false; }
        resetStats(); peer_=s; return true;
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
                    sent_+=static_cast<size_t>(r); add(txBytes_,r); progress=true;
                    if(sent_==tx_.size()) { tx_.clear(); sent_=0; add(txPackets_,1); }
                } else if(r==0||!retryable()) return fail(2);
            }
            if(!ready_) {
                size_t old=rx_.size(),n=std::min<size_t>(65536,target_-old);
                rx_.resize(old+n);
                auto r=recv(peer_,reinterpret_cast<char*>(rx_.data()+old),static_cast<int>(n),0);
                if(r<=0) {
                    rx_.resize(old);
                    if(r==0) return fail(1);
                    if(!retryable()) return fail(2);
                } else {
                    rx_.resize(old+static_cast<size_t>(r)); add(rxBytes_,r); progress=true;
                    if(rx_.size()==target_) {
                        if(target_==headerSize) {
                            if(rx_[0]!='C'||rx_[1]!='K'||rx_[2]!='M'||rx_[3]!='P'||
                               rx_[4]!=1||rx_[5]<1||rx_[5]>7||rx_[6]||rx_[7]||
                               !get(rx_,8,4)||get(rx_,16,8)>0x7fffffffffffffffull||
                               get(rx_,24,4)>maxPayload) return fail(3);
                            target_=headerSize+static_cast<size_t>(get(rx_,24,4));
                        }
                        if(rx_.size()==target_) {
                            Header h; Bytes payload;
                            if(!decode(rx_,h,payload)) return fail(3);
                            ready_=true; add(rxPackets_,1);
                        }
                    }
                }
            }
            return progress?1:0;
        } catch(...) { return fail(4); }
    }
};
}
