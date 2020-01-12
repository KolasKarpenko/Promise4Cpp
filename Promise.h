#pragma once

#include <cassert>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

class IPromise
{
public:
	typedef std::shared_ptr<IPromise> PromisePtr;

	IPromise(const IPromise&) = delete;

	virtual ~IPromise()
	{
	}

	virtual void Run() = 0;

	static void WaitForFinished()
	{
		while (true) {
			{
				std::lock_guard<std::mutex> lock(ms_poolMutex);
				if (ms_pool.empty()) {
					break;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
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
		static size_t lastId = 0;
		m_id = lastId++;
	}

	static void PushPull(const PromisePtr& p)
	{
		assert(p);
		{
			std::lock_guard<std::mutex> lock(ms_poolMutex);
			ms_pool.insert(std::make_pair(p->m_id, p));
		}
		p->Run();
	}

	static void PopPull(size_t id)
	{
		std::lock_guard<std::mutex> lock(ms_poolMutex);
		ms_pool.erase(id);
	}

	State m_state;
	size_t m_id;
	static std::mutex ms_poolMutex;
	static std::map<size_t, PromisePtr> ms_pool;
};

std::mutex IPromise::ms_poolMutex;
std::map<size_t, IPromise::PromisePtr> IPromise::ms_pool;

template<typename TResult>
class TPromise : public IPromise
{
public:
	typedef std::string TError;
	typedef std::function<void(const TResult & result)> OnResolveFunc;
	typedef std::function<void(const TError & error)> OnRejectFunc;
	typedef std::function<void(const OnResolveFunc & resolve, const OnRejectFunc & reject)> PromiseFunc;
	typedef std::shared_ptr<TPromise<TResult>> PromisePtr;

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
	TPromise(const PromiseFunc& impl) : IPromise(), m_impl(impl)
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

		PopPull(m_id);
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

		PopPull(m_id);
	}

	PromiseFunc m_impl;
	TResult m_result;
	TError m_error;
	std::vector<std::pair<OnResolveFunc, OnRejectFunc>> m_handlers;
	std::mutex m_mutex;
};

template<typename TResult>
class Promise : public TPromise<TResult>
{
private:
	class Async : public TPromise<TResult>
	{
	public:
		Async(const PromiseFunc& impl)
			: TPromise<TResult>(impl)
		{
		}

		virtual void Run() override
		{
			m_thread = std::thread(m_impl,
				[this](const TResult& result) { Resolve(result); },
				[this](const TError& error) { Reject(error); }
			);
		}

		~Async()
		{
			m_thread.join();
		}
	private:
		std::thread m_thread;
	};

public:
	virtual void Run() override
	{
		m_impl(
			[this](const TResult& result) { Resolve(result); },
			[this](const TError& error) { Reject(error); }
		);
	}

	static PromisePtr Create(const PromiseFunc& impl)
	{
		Promise<TResult>::PromisePtr ptr(new Promise(impl));
		PushPull(ptr);
		return ptr;
	}

	static PromisePtr CreateAsync(const PromiseFunc& impl)
	{
		TPromise<TResult>::PromisePtr ptr(new Async(impl));
		PushPull(ptr);
		return ptr;
	}

	static std::shared_ptr<TPromise<std::vector<TResult>>> All(const std::vector<TPromise<TResult>::PromisePtr>& all)
	{
		return Promise<std::vector<TResult>>::Create(
			[all](const Promise<std::vector<TResult>>::OnResolveFunc& resolve, const Promise<std::vector<TResult>>::OnRejectFunc& reject) {
				std::shared_ptr<std::atomic<size_t>> count(new std::atomic<size_t>(all.size()));
				std::shared_ptr<std::vector<TResult>> result(new std::vector<TResult>(all.size()));
				for (size_t i = 0; i < all.size(); ++i) {
					all[i]->Then(
						[i, result, count, resolve](const TResult& res) {
							(*result)[i] = res;
							(*count)--;

							if (*count == 0) {
								resolve(*result);
							}
						},
						[reject](const TError& err) {
							reject(err);
						}
					);
				}
			}
		);
	}

private:
	Promise(const PromiseFunc& impl) : TPromise(impl)
	{}
};

