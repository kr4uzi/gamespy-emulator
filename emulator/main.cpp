#include "asio.h"
#include "emulator.h"
#include <csignal>
#include <iostream>
#include <print>

int main(int argc, char* argv[])
{
	try {
		boost::asio::io_context context;
		gamespy::Emulator emulator{ context };
		boost::asio::cancellation_signal shutdown;
		boost::asio::signal_set signals{ context, SIGINT, SIGTERM };
		bool stopping = false;
		int result = 0;
		signals.async_wait([&](auto error, auto) {
			if (error) return;
			stopping = true;
			std::println("SHUTDOWN REQUESTED");
			emulator.Stop();
			shutdown.emit(boost::asio::cancellation_type::all);
		});

		auto report = [&](std::exception_ptr error) {
			if (!error) return;
			try {
				std::rethrow_exception(error);
			} catch (const boost::system::system_error& e) {
				if (stopping && e.code() == boost::asio::error::operation_aborted) return;
				std::println(std::cerr, "[ERR] {}", e.what());
			} catch (const std::exception& e) {
				std::println(std::cerr, "[ERR] {}", e.what());
			}
			result = 1;
		};
		boost::asio::co_spawn(context, emulator.Launch(argc, argv),
			boost::asio::bind_cancellation_slot(shutdown.slot(), [&](std::exception_ptr error) {
				report(error);
				boost::asio::co_spawn(context, emulator.Shutdown(), [&](std::exception_ptr cleanupError) {
					report(cleanupError);
					signals.cancel();
				});
			}));
		// Drain canceled sessions before destroying their servers and databases.
		context.run();
		return result;
	} catch (const std::exception& e) {
		std::println(std::cerr, "[ERR] {}", e.what());
		return 1;
	}
}
