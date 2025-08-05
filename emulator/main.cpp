#include "asio.h"
#include "emulator.h"
#include <string_view>
#include <csignal>
#include <iostream>
#include <print>

int main(int argc, char *argv[])
{
	try {
		auto context = boost::asio::io_context{};
		auto signals = boost::asio::signal_set{ context, SIGINT, SIGTERM };
		signals.async_wait([&](auto, auto) {
			std::println("SHUTDOWN REQUESTED");
			context.stop();
		});

		auto emulator = gamespy::Emulator{ context };
		boost::asio::co_spawn(context, emulator.Launch(argc, argv), [&](std::exception_ptr ex) {
			if (ex) {
				try {
					std::rethrow_exception(ex);
				}
				catch (std::exception& e) {
					std::println(std::cerr, "[exception] {}", e.what());
					context.stop();
				}
			}
		});

		context.run();
	}
	catch (std::exception& e) {
		std::println(std::cerr, "[ERR] {}", e.what());
		return 1;
	}

	return 0;
}