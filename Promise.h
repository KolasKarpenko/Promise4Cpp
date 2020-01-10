#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <thread>

template<typename TResult>
class Promise
{
public:
	typedef std::string TError;
	typedef std::function<void(const TResult & result)> OnResolveFunc;
	typedef std::function<void(const TError & error)> OnRejectFunc;
	typedef std::function<void(const OnResolveFunc & resolve, const OnRejectFunc & reject)> PromiseFunc;
	typedef std::shared_ptr<Promise<TResult>> PromisePtr;

	Promise(const Promise&) = delete;

	void Then(const OnResolveFunc& resolve, const OnRejectFunc& reject)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		switch (m_state)
		{
		case State::Pending:
			m_handlers.push_back(std::make_pair(resolve, reject));
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

protected:
	enum class State
	{
		Pending = 0,
		Resolved,
		Rejected
	};

	Promise() : m_state(State::Pending)
	{
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

template<typename TResult>
class SyncPromise : public Promise<TResult>
{
public:
	static Promise<TResult>::PromisePtr Create(const PromiseFunc& impl)
	{
		Promise<TResult>::PromisePtr ptr(new SyncPromise(impl));
		return ptr;
	}

protected:
	SyncPromise(const PromiseFunc& impl) : Promise<TResult>()
	{
		impl(
			[this](const TResult& result) { Resolve(result); },
			[this](const TError& error) { Reject(error); }
		);
	}
};

template<typename TResult>
class AsyncPromise : public Promise<TResult>
{
public:
	static Promise<TResult>::PromisePtr Create(const PromiseFunc& impl)
	{
		Promise<TResult>::PromisePtr ptr(new AsyncPromise(impl));
		return ptr;
	}

	~AsyncPromise()
	{
		m_thread.join();
	}

protected:
	AsyncPromise(const PromiseFunc& impl)
		: Promise<TResult>(),
		m_thread(impl,
			[this](const TResult& result) { Resolve(result); },
			[this](const TError& error) { Reject(error); }
		)
	{
	}

private:
	std::thread m_thread;
};
