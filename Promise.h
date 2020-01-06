#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>

template<typename TResult>
class Promise
{
public:
	typedef std::string TError;
	typedef std::function<void(const TResult & result)> OnResolveFunc;
	typedef std::function<void(const TError & error)> OnRejectFunc;
	typedef std::function<void(const OnResolveFunc & resolve, const OnRejectFunc & reject)> PromiseFunc;
	typedef std::shared_ptr<Promise<TResult>> PromisePtr;

	Promise() = delete;
	Promise(const Promise&) = delete;

	static PromisePtr Create(const PromiseFunc& impl)
	{
		PromisePtr ptr(new Promise(impl));
		return ptr;
	}

	void Then(const OnResolveFunc& resolve, const OnRejectFunc& reject)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		switch (m_state)
		{
		case State::Pending:
		{
			m_handlers.push_back(std::make_pair(resolve, reject));
		}
		break;
		case State::Resolved:
			resolve(m_result);
			break;
		case State::Rejected:
			reject(m_error);
			break;
		default:
			break;
		}
	}

private:
	enum class State
	{
		Pending = 0,
		Resolved,
		Rejected
	};

	Promise(const PromiseFunc& impl) : m_state(State::Pending)
	{
		impl(
			[this](const TResult& result) { Resolve(result); },
			[this](const TError& error) { Reject(error); }
		);
	}

	void Resolve(const TResult& result) {
		std::lock_guard<std::mutex> lock(m_mutex);

		if (m_state != State::Pending) {
			return;
		}

		m_state = State::Resolved;
		m_result = result;
		for (const auto& cb : m_handlers) {
			cb.first(m_result);
		}
	}

	void Reject(const TError& error) {
		std::lock_guard<std::mutex> lock(m_mutex);

		if (m_state != State::Pending) {
			return;
		}

		m_state = State::Rejected;
		m_error = error;
		for (const auto& cb : m_handlers) {
			cb.second(m_error);
		}
	}

	State m_state;
	TResult m_result;
	TError m_error;
	std::vector<std::pair<OnResolveFunc, OnRejectFunc>> m_handlers;
	std::mutex m_mutex;
};
