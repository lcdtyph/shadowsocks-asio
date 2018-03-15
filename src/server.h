#ifndef __SERVER_H__
#define __SERVER_H__

#include <cstdint>
#include <boost/asio.hpp>

#include "common.h"
#include "socks5.h"
#include "buffer.h"


class Socks5ProxyServer {
    typedef boost::asio::ip::tcp tcp;
public:
    Socks5ProxyServer(boost::asio::io_context &ctx, uint16_t port)
        : acceptor_(ctx, tcp::endpoint(tcp::v4(), port)) {
        DoAccept();
    }

private:
    void DoAccept();

    tcp::acceptor acceptor_;
};

#endif

