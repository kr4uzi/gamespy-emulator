#pragma once
#include "asio.h"
#include "task.h"
#include <iostream>
#include <cstddef>
#include <memory>
#include <set>

namespace gamespy {
	// Used on the io_context thread. Keep every spawned session cancellable and
	// wait for its coroutine to finish before releasing the shared databases.
	class Sessions {
		struct State {
			boost::asio::steady_timer drained;
			std::set<std::shared_ptr<boost::asio::cancellation_signal>> active;
			const std::size_t limit;
			bool stopped = false;

			State(boost::asio::any_io_executor executor, std::size_t maxActive)
				: drained(executor, boost::asio::steady_timer::time_point::max()), limit(maxActive) {}
		};
		std::shared_ptr<State> m_State;

		static task<void> Run(std::shared_ptr<State> state, task<void> session)
		{
			// Stop may run before co_spawn dispatches the first coroutine step.
			if (!state->stopped) co_await std::move(session);
		}

	public:
		explicit Sessions(boost::asio::any_io_executor executor, std::size_t limit = 4096)
			: m_State(std::make_shared<State>(executor, limit)) {}

		void Spawn(task<void> session)
		{
			auto state = m_State;
			if (state->stopped || state->active.size() >= state->limit) return;
			auto signal = std::make_shared<boost::asio::cancellation_signal>();
			state->active.insert(signal);
			try {
				boost::asio::co_spawn(state->drained.get_executor(), Run(state, std::move(session)),
					boost::asio::bind_cancellation_slot(signal->slot(), [state, signal](std::exception_ptr error) {
						state->active.erase(signal);
						if (state->active.empty()) state->drained.cancel();
						if (error) {
							try { std::rethrow_exception(error); }
							catch (const boost::system::system_error& e) {
								if (e.code() != boost::asio::error::operation_aborted)
									std::cerr << "[session] " << e.what() << '\n';
							}
							catch (const std::exception& e) { std::cerr << "[session] " << e.what() << '\n'; }
							catch (...) { std::cerr << "[session] unknown exception\n"; }
						}
					}));
			}
			catch (...) {
				state->active.erase(signal);
				if (state->active.empty()) state->drained.cancel();
				throw;
			}
		}

		void Stop()
		{
			if (m_State->stopped) return;
			m_State->stopped = true;
			// Emitting may complete a session; retain signals during the iteration.
			const auto pending = m_State->active;
			for (const auto& signal : pending)
				signal->emit(boost::asio::cancellation_type::all);
		}

		task<void> Wait()
		{
			auto state = m_State;
			while (!state->active.empty())
				co_await state->drained.async_wait(boost::asio::as_tuple(boost::asio::use_awaitable));
		}
	};
}
