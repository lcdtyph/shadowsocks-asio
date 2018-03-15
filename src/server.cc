
#include <utility>
#include <memory>
#include <algorithm>
#include <boost/log/trivial.hpp>
#include <boost/endian/conversion.hpp>

#include "server.h"

#define MAX_LENGTH 8192

using boost::asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
    struct Peer {
        Peer(tcp::socket socket, size_t len)
            : socket(std::move(socket)), buf(len) {
        }

        Peer(boost::asio::io_context &ctx, size_t len)
            : socket(ctx), buf(len) {
        }

        tcp::socket socket;
        Buffer buf;
    };

public:

    Session(tcp::socket socket)
        : context(socket.get_executor().context()),
          client_(std::move(socket), MAX_LENGTH),
          remote_(context, MAX_LENGTH),
          resolver_(context) {
    }

    ~Session() {
        LOG(TRACE) << "Session completed: " << client_.socket.remote_endpoint();
    }

    void Start() {
        LOG(TRACE) << "Start read method selection message";
        DoReadSocks5MethodSelectionMessage();
    }

private:
    void DoReadSocks5MethodSelectionMessage() {
        auto self(shared_from_this());
        client_.socket.async_read_some(
            client_.buf.get_buffer(),
            [this, self](boost::system::error_code ec, size_t len) {
                if (ec) {
                    if (ec == boost::asio::error::misc_errors::eof) {
                        LOG(TRACE) << "Got EOF";
                        return;
                    }
                    LOG(WARNING) << "Error: " << ec; 
                    return;
                }
                client_.buf.Append(len);
                auto *hdr = (socks5::MethodSelectionMessageHeader *)(client_.buf.get_data());
                if (hdr->ver != socks5::VERSION) {
                    LOG(WARNING) << "Unsupport socks version: " << (uint32_t)hdr->ver;
                    return;
                }
                size_t need_more = socks5::MethodSelectionMessageHeader::NeedMore(
                                        client_.buf.get_data(), client_.buf.Size());
                if (need_more) {
                    LOG(TRACE) << "need more data, current: " << client_.buf.Size()
                               << "excepted more: " << need_more;
                    DoReadSocks5MethodSelectionMessage();
                    return;
                }
                uint8_t method_selected = socks5::NO_ACCCEPTABLE_METHOD;
                for (uint8_t i = 0; i < hdr->num_methods; ++i) {
                    if (hdr->methods[i] == socks5::NO_AUTH_METHOD) {
                        method_selected = hdr->methods[i];
                        break;
                    }
                }
                LOG(TRACE) << "Start write reply";
                DoWriteSocks5MethodSelectionReply(method_selected);
            }
        );
    }

    void DoWriteSocks5MethodSelectionReply(uint8_t method) {
        auto self(shared_from_this());
        auto *hdr = (socks5::MethodSelectionMessageReply *)(client_.buf.get_data());
        hdr->ver = socks5::VERSION;
        hdr->method = method;
        client_.buf.Reset(2);
        boost::asio::async_write(
            client_.socket, client_.buf.get_const_buffer(),
            [this, self, method](boost::system::error_code ec, size_t len) {
                if (ec) {
                    LOG(WARNING) << "Unexcepted error: " << ec;
                    return;
                }
                client_.buf.Reset();
                if (method == socks5::NO_AUTH_METHOD) {
                    DoReadSocks5Request();
                }
            }
        );
    }

    void DoReadSocks5Request(size_t at_least = 4) {
        auto self(shared_from_this());
        boost::asio::async_read(
            client_.socket,
            client_.buf.get_buffer(),
            boost::asio::transfer_at_least(at_least),
            [this, self](boost::system::error_code ec, size_t len) {
                if (ec) {
                    LOG(WARNING) << "Unexcepted error: " << ec;
                    return;
                }
                client_.buf.Append(len);
                size_t need_more = socks5::Request::NeedMore(client_.buf.get_data(),
                                                             client_.buf.Size());
                if (need_more) {
                    LOG(TRACE) << "Need more data: " << need_more;
                    DoReadSocks5Request(need_more);
                    return;
                }
                auto *hdr = (socks5::Request *)(client_.buf.get_data());
                if (hdr->ver != socks5::VERSION) {
                    LOG(WARNING) << "Unsupport socks version: " << (uint32_t)hdr->ver;
                    return;
                }
                if (hdr->cmd != socks5::CONNECT_CMD) {
                    LOG(DEBUG) << "Unsupport command: " << hdr->cmd;
                    DoWriteSocks5Reply(socks5::CMD_NOT_SUPPORTED_REP);
                    return;
                }

                std::string address;
                std::string port;
                size_t port_offset;
                switch(hdr->atype) {
                case socks5::IPV4_ATYPE:
                    std::array<uint8_t, 4> ipv4_buf;
                    port_offset = ipv4_buf.size();
                    std::copy_n(&hdr->variable_field[0], port_offset, std::begin(ipv4_buf));
                    address = boost::asio::ip::make_address_v4(ipv4_buf).to_string();
                    break;

                case socks5::DOMAIN_ATYPE:
                    port_offset = hdr->variable_field[0];
                    std::copy_n(&hdr->variable_field[1], port_offset,
                                std::back_inserter(address));
                    port_offset += 1;
                    break;

                case socks5::IPV6_ATYPE:
                    std::array<uint8_t, 16> ipv6_buf;
                    port_offset = ipv6_buf.size();
                    std::copy_n(&hdr->variable_field[0], port_offset, std::begin(ipv6_buf));
                    address = boost::asio::ip::make_address_v6(ipv6_buf).to_string();
                    break;

                default:
                    LOG(DEBUG) << "Unsupport address type: " << hdr->atype;
                    DoWriteSocks5Reply(socks5::ATYPE_NOT_SUPPORTED_REP);
                    return;
                }
                port = std::to_string(boost::endian::big_to_native(
                                    *(uint16_t *)(&hdr->variable_field[port_offset])));
                LOG(TRACE) << "Resolving to " << address << ":" << port;
                DoResolveRemote(std::move(address), std::move(port));
            }
        );
    }

    void DoResolveRemote(std::string host, std::string port) {
        auto self(shared_from_this());
        resolver_.async_resolve(host, port,
            [this, self](boost::system::error_code ec, tcp::resolver::iterator itr) {
                if (ec) {
                    LOG(DEBUG) << "Unable to resolve: " << ec;
                    DoWriteSocks5Reply(socks5::HOST_UNREACHABLE_REP);
                    return;
                }
                DoConnectRemote(itr);
            }
        );
    }

    void DoConnectRemote(tcp::resolver::iterator itr) {
        auto self(shared_from_this());
        boost::asio::async_connect(remote_.socket, itr,
            [this, self](boost::system::error_code ec, tcp::resolver::iterator itr) {
                if (ec || itr == tcp::resolver::iterator()) {
                    LOG(DEBUG) << "Cannot connect to remote: " << ec;
                    DoWriteSocks5Reply((itr == tcp::resolver::iterator()
                                        ? socks5::CONN_REFUSED_REP
                                        : socks5::NETWORK_UNREACHABLE_REP));
                    return;
                }
                LOG(DEBUG) << "Connected to remote " << itr->host_name();
                DoWriteSocks5Reply(socks5::SUCCEEDED_REP);
            }
        );
    }

    void DoWriteSocks5Reply(uint8_t reply) {
        auto self(shared_from_this());
        auto *hdr = (socks5::Reply *)(client_.buf.get_data());
        hdr->rsv = 0;
        hdr->rep = reply;
        client_.buf.Reset(
                socks5::Reply::FillBoundAddress(client_.buf.get_data(),
                                                remote_.socket.local_endpoint()));
        boost::asio::async_write(client_.socket, client_.buf.get_const_buffer(),
            [this, self, reply](boost::system::error_code ec, size_t len) {
                if (ec) {
                    LOG(WARNING) << "Unexcepted write error " << ec;
                    return;
                }
                if (reply == socks5::SUCCEEDED_REP) {
                    LOG(TRACE) << "Start streaming";
                    client_.buf.Reset();
                    StartStream();
                }
            }
        );
    }

    void StartStream() {
        DoRelayStream(client_, remote_);
        DoRelayStream(remote_, client_);
    }

    void DoRelayStream(Peer &src, Peer &dest) {
        auto self(shared_from_this());
        src.socket.async_read_some(
            src.buf.get_buffer(),
            [this, self, &src, &dest](boost::system::error_code ec, size_t len) {
                if (ec) {
                    if (ec == boost::asio::error::misc_errors::eof) {
                        LOG(TRACE) << "Stream terminates normally";
                        return;
                    }
                    LOG(WARNING) << "Unexcepted error: " << ec;
                }
                src.buf.Reset(len);
                boost::asio::async_write(dest.socket,
                    src.buf.get_const_buffer(),
                    [this, self, &src, &dest](boost::system::error_code ec, size_t len) {
                        if (ec) {
                            LOG(WARNING) << "Unexcepted error: " << ec;
                            return;
                        }
                        src.buf.Reset();
                        DoRelayStream(src, dest);
                    }
                );
            }
        );
    }

    boost::asio::io_context &context;
    Peer client_;
    Peer remote_;
    tcp::resolver resolver_;
};

void Socks5ProxyServer::DoAccept() {
    acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
        if (!ec) {
            LOG(INFO) << "A new client accepted: " << socket.remote_endpoint();
            std::make_shared<Session>(std::move(socket))->Start();
        }
        DoAccept();
    });
}
