#include <cstdlib>
#include <fstream>
#include <iostream>
#include <boost/program_options.hpp>

#include <crypto_utils/crypto.h>
#include <ss_proto/client.h>

#include "parse_args.h"

namespace bpo = boost::program_options;
using boost::asio::ip::tcp;

void ParseArgs(int argc, char *argv[], StreamServerArgs *args,
               ResolverArgs *rargs, int *log_level, Plugin *p) {
    auto factory = CryptoContextGeneratorFactory::Instance();
    bpo::options_description desc("Socks5 Proxy Server");
    desc.add(*GetCommonOptions()).add_options()
        ("server-address,s", bpo::value<std::string>(), "Server address")
        ("server-port,p", bpo::value<uint16_t>()->default_value(8088), "Server port")
        ("method,m", bpo::value<std::string>(), "Cipher method")
        ("password,k", bpo::value<std::string>(), "Password")
        ("plugin", bpo::value<std::string>(), "Plugin executable name")
        ("plugin-opts", bpo::value<std::string>(), "Plugin options");

    bpo::variables_map vm;
    bpo::store(bpo::parse_command_line(argc, argv, desc), vm);
    bpo::notify(vm);
    if (vm.count("help")) {
        std::cout << desc << std::endl;
        std::vector<std::string> methods;
        factory->GetAllRegisteredNames(methods);
        std::cout << "Available methods:\n   ";
        for (auto &m : methods) {
            std::cout << " " << m;
        }
        std::cout << std::endl;
        exit(0);
    }

    if (vm.count("config-file")) {
        auto filename = vm["config-file"].as<std::string>();
        std::ifstream ifs(filename);
        if (!ifs) {
            std::cerr << "Unavailable configure file" << std::endl;
            exit(-1);
        }
        bpo::store(bpo::parse_config_file(ifs, desc), vm);
        bpo::notify(vm);
    }

    uint16_t bind_port = vm["bind-port"].as<uint16_t>();
    boost::system::error_code ec;
    auto bind_address = boost::asio::ip::make_address(vm["bind-address"].as<std::string>(), ec);
    if (ec) {
        std::cerr << "Invalid bind address" << std::endl;
        exit(-1);
    }
    args->bind_ep = tcp::endpoint(bind_address, bind_port);
    args->timeout = vm["timeout"].as<size_t>() * 1000;

    if (!vm.count("server-address")) {
        std::cerr << "Please specify the server address" << std::endl;
        exit(-1);
    }
    std::string server_host = vm["server-address"].as<std::string>();
    uint16_t server_port = vm["server-port"].as<uint16_t>();

    if (!vm.count("password")) {
        std::cerr << "Please specify the password" << std::endl;
        exit(-1);
    }
    std::string password = vm["password"].as<std::string>();

    *log_level = vm["verbose"].as<int>();

    if (vm.count("plugin")) {
        std::string plugin = vm["plugin"].as<std::string>();
        if (!plugin.empty()) {
            p->enable = true;
            p->plugin = plugin;
            p->remote_address = server_host;
            p->remote_port = server_port;
            p->local_address = "127.0.0.1";
            p->local_port = GetFreePort();
            if (p->local_port == 0) {
                std::cerr << "Fatal error: cannot get a freedom port" << std::endl;
                exit(-1);
            }
            if (vm.count("plugin-opts")) {
                p->plugin_options = vm["plugin-opts"].as<std::string>();
            }

            server_host = p->local_address;
            server_port = p->local_port;
        }
    }
    auto remote_target = std::make_shared<TargetInfo>(MakeTarget(server_host, server_port));
    if (remote_target->IsEmpty()) {
        std::cerr << "Invalid server host / port" << std::endl;
        exit(-1);
    }

    if (!vm.count("method")) {
        std::cerr << "Please specify a cipher method using -m option" << std::endl;
        exit(-1);
    }
    auto crypto_generator = factory->GetGenerator(vm["method"].as<std::string>(), password);
    if (!crypto_generator) {
        std::cerr << "Invalid cipher type!" << std::endl;
        exit(-1);
    }

    GetResolverArgs(vm, rargs);

    args->generator = \
        [target = std::move(remote_target), g = *crypto_generator]() {
            return GetProtocol<ShadowsocksClient>(target, g());
        };
    return;
}

