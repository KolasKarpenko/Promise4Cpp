#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

template<typename TResult>
class IPromise
{
public:
	typedef std::string TError;
	typedef std::function<void(const TResult & result)> OnResolveFunc;
	typedef std::function<void(const TError & error)> OnRejectFunc;
	typedef std::function<void(const OnResolveFunc & resolve, const OnRejectFunc & reject)> PromiseFunc;
	typedef std::shared_ptr<IPromise<TResult>> PromisePtr;

	IPromise(const IPromise&) = delete;

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

	bool Result(TResult& result, TError& error)
	{
		std::atomic<bool> resolved = false;
		std::atomic<bool> ok = true;

		Then(
			[&result, &resolved](const TResult& value) {
				result = value;
				resolved = true;
			},
			[&error, &resolved, &ok](const TError& value) {
				error = value;
				resolved = true;
				ok = false;
			}
		);

		while (!resolved) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		return ok;
	}

protected:
	enum class State
	{
		Pending = 0,
		Resolved,
		Rejected
	};

	IPromise() : m_state(State::Pending)
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
class Promise : public IPromise<TResult>
{
private:
	class Async : public IPromise<TResult>
	{
	public:
		Async(const PromiseFunc& impl)
			: IPromise<TResult>(),
			m_thread(impl,
				[this](const TResult& result) { Resolve(result); },
				[this](const TError& error) { Reject(error); }
			)
		{
		}

		~Async()
		{
			m_thread.join();
		}
	private:
		std::thread m_thread;
	};

public:
	static PromisePtr Create(const PromiseFunc& impl)
	{
		IPromise<TResult>::PromisePtr ptr(new Promise(impl));
		return ptr;
	}

	static PromisePtr CreateAsync(const PromiseFunc& impl)
	{
		IPromise<TResult>::PromisePtr ptr(new Async(impl));
		return ptr;
	}

private:
	Promise(const PromiseFunc& impl) : IPromise<TResult>()
	{
		impl(
			[this](const TResult& result) { Resolve(result); },
			[this](const TError& error) { Reject(error); }
		);
	}
};

