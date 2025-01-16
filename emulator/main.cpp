#include "server.h"
#include "asio.h"
#include "config.h"
#include <csignal>
#include <print>
#include <iostream>
#include <boost/program_options.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include "utils.h"

int main(int argc, char **argv)
{
	try {
		namespace po = boost::program_options;
		auto options = po::options_description{ "Allowed options" };
		options.add_options()
			("version,v", "print version string")
			("help", "produces this help message")
			("create-config", po::value<std::string>(), "creates a default config file")
			("config", po::value<std::string>(), "uses the supplied config file")
		;

		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, options), vm);
		po::notify(vm);

		if (vm.count("help")) {
			std::cout << options << std::endl;
			return EXIT_SUCCESS;
		}

		if (vm.count("create-config")) {
			gamespy::Config::CreateTemplate(vm.at("create-config").as<std::string>());
			return EXIT_SUCCESS;
		}

		auto config = [](auto configFile) {
			if (!configFile.empty())
				return gamespy::Config{ configFile };

			if (std::filesystem::exists("default.ini"))
				return gamespy::Config{ "default.ini" };

			return gamespy::Config{};
		}(vm.count("config") ? vm.at("config").as<std::string>() : std::string{});

		auto context = boost::asio::io_context{};
		auto signals = boost::asio::signal_set{ context, SIGINT, SIGTERM };
		signals.async_wait([&](auto, auto) {
			std::println("SHUTDOWN REQUESTED");
			context.stop();
		});

		auto server = gamespy::Server{ config, context };
		boost::asio::co_spawn(context, server.Run(), [](std::exception_ptr ex) {
			if (ex) {
				try {
					std::rethrow_exception(ex);
				}
				catch (std::exception& e) {
					std::println(std::cerr, "[exception] {}", e.what());
				}
			}
		});

		// strings / urls found in bf2 files
		// http://eapusher.dice.se/image.asp?lang=English
		// gpsp.gamespy.com TCP 29901
		// gpcm.gamespy.com TCP 29900
		// gamestats.gamespy.com TCP 29920 <-- not used?
		// %s.master.gamespy.com UDP 27900
		// %s.ms%d.gamespy.com TCP 28910
		// %s.available.gamespy.com UDP 27900 (same server as master)
		// http://BF2Web.gamespy.com/ASP/
		// http://stage-net.gamespy.com/bf2/getplayerinfo.aspx?pid=

		context.run();
	}
	catch (std::exception& e) {
		std::println(std::cerr, "[ERR] {}", e.what());
	}
}