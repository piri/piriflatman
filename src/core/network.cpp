#include "piriflatman/network.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
namespace pfm {
static std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    return s.substr(first, s.find_last_not_of(" \t") - first + 1);
}
HttpRequest::State HttpRequest::feed(const char* bytes, size_t length) {
    if (state_ != State::Reading) return state_;
    buffer_.append(bytes, length);
    if (!headers_) {
        const auto end = buffer_.find("\r\n\r\n");
        if (end == std::string::npos) {
            if (buffer_.size() > 4096) state_ = State::Invalid;
            return state_;
        }
        if (end > 4096) return state_ = State::Invalid;
        std::istringstream input(buffer_.substr(0, end));
        std::string line, target, version, extra;
        std::getline(input, line);
        std::istringstream first(line);
        if (!(first >> request_.method >> target >> version) || (first >> extra) ||
            (version != "HTTP/1.1" && version != "HTTP/1.0") || target.empty() || target[0] != '/')
            return state_ = State::Invalid;
        auto query = target.find('?');
        request_.path = target.substr(0, query);
        if (query != std::string::npos) request_.query = target.substr(query+1);
        bool gotLength = false, gotType = false;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto colon = line.find(':');
            if (colon == std::string::npos || colon == 0 || line[0] == ' ' || line[0] == '\t') return state_ = State::Invalid;
            auto key = lower(line.substr(0, colon));
            auto value = trim(line.substr(colon+1));
            if (key == "content-length") {
                if (gotLength || !unsignedInteger(value, contentLength_) || contentLength_ > 2048) return state_ = State::Invalid;
                gotLength = true;
            } else if (key == "content-type") {
                if (gotType) return state_ = State::Invalid;
                request_.contentType = value; gotType = true;
            } else if (key == "transfer-encoding") return state_ = State::Invalid;
            else if (key == "expect") {
                if (lower(value) != "100-continue") return state_ = State::Invalid;
                continue_ = true;
            }
        }
        // PUT requests with a body require Content-Length. Chunked requests are rejected.
        buffer_.erase(0, end+4);
        headers_ = true;
    }
    if (buffer_.size() > contentLength_) return state_ = State::Invalid;
    if (buffer_.size() == contentLength_) {
        request_.body = std::move(buffer_);
        continue_ = false;
        state_ = State::Complete;
    }
    return state_;
}
bool HttpRequest::takeContinue() { bool result = continue_; continue_ = false; return result; }
static int makeSocket(int type, uint16_t port) {
    int fd = socket(AF_INET, type, 0);
    if (fd < 0) return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    if (type == SOCK_DGRAM) setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = htons(port);
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0 || bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        (type == SOCK_STREAM && listen(fd, 4) < 0)) { close(fd); return -1; }
    return fd;
}
bool NetworkServer::begin(uint16_t httpPort, uint16_t discoveryPort) {
    stop();
    port_ = httpPort;
    http_ = makeSocket(SOCK_STREAM, httpPort);
    udp_ = makeSocket(SOCK_DGRAM, discoveryPort);
    if (http_ < 0 || udp_ < 0) { stop(); return false; }
    return true;
}
void NetworkServer::closeClient(Client& c) {
    if (c.fd >= 0) close(c.fd);
    c = Client{};
}
void NetworkServer::stop() {
    for (auto& c : clients_) closeClient(c);
    if (http_ >= 0) close(http_);
    if (udp_ >= 0) close(udp_);
    http_ = udp_ = -1;
}
void NetworkServer::tick(uint32_t now) {
    if (http_ < 0) return;
    for (auto& c : clients_) {
        if (c.fd < 0) {
            c.fd = accept(http_, nullptr, nullptr);
            if (c.fd >= 0) {
                if (fcntl(c.fd, F_SETFL, O_NONBLOCK) < 0) { closeClient(c); break; }
                c.opened = now;
            }
            break; // Accept at most one per pass.
        }
    }
    char buffer[1024];
    for (auto& c : clients_) {
        if (c.fd < 0) continue;
        if (static_cast<uint32_t>(now - c.opened) >= 5000) { closeClient(c); continue; }
        if (c.output.empty()) {
            const int n = recv(c.fd, buffer, sizeof(buffer), 0);
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) { closeClient(c); continue; }
            if (n > 0) {
                const auto state = c.parser.feed(buffer, n);
                if (state == HttpRequest::State::Complete) { c.output = api_.handle(c.parser.request()).wire(); c.final = true; }
                else if (state == HttpRequest::State::Invalid) { c.output = Response{400, "text/plain", "Invalid or oversized HTTP request"}.wire(); c.final = true; }
                else if (c.parser.takeContinue()) c.output = "HTTP/1.1 100 Continue\r\n\r\n";
            }
        }
        if (!c.output.empty()) {
#ifdef MSG_NOSIGNAL
            constexpr int flags = MSG_NOSIGNAL;
#else
            constexpr int flags = 0;
#endif
            const int n = send(c.fd, c.output.data() + c.sent, std::min<size_t>(1024, c.output.size() - c.sent), flags);
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) { closeClient(c); continue; }
            if (n > 0) c.sent += n;
            if (c.sent == c.output.size()) {
                if (c.final) closeClient(c);
                else { c.output.clear(); c.sent = 0; }
            }
        }
    }
    // One datagram per pass keeps discovery floods from starving panel/Wi-Fi work.
    sockaddr_in peer{};
    socklen_t size = sizeof(peer);
    const int n = recvfrom(udp_, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&peer), &size);
    if (n > 0 && discoveryMessage(std::string(buffer, n))) {
        const auto response = "{\"AlpacaPort\":" + std::to_string(port_) + "}";
        sendto(udp_, response.data(), response.size(), 0, reinterpret_cast<sockaddr*>(&peer), size);
    }
}
}
