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

	virtual void Reset() = 0;

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
		p->Reset();
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
	typedef std::function<void(const TResult& result)> OnResolveFunc;
	typedef std::function<void(const TError& error)> OnRejectFunc;
	typedef std::function<void(int progress)> OnProgressFunc;
	typedef std::function<void(
		const OnResolveFunc& resolve,
		const OnRejectFunc& reject,
		const OnProgressFunc& progress
	)> PromiseFunc;
	typedef std::shared_ptr<TPromise<TResult>> TPromisePtr;

	void Then(const OnResolveFunc& resolve)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_resolveHandlers.push_back(resolve);

		switch (m_state)
		{
		case State::Pending:
			break;
		case State::Resolved:
			resolve(m_result);
			break;
		default:
			break;
		}
	}

	void Then(const OnResolveFunc& resolve, const OnProgressFunc& progress)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_resolveHandlers.push_back(resolve);
		m_progressHandlers.push_back(progress);

		switch (m_state)
		{
		case State::Pending:
			break;
		case State::Resolved:
			progress(100);
			resolve(m_result);
			break;
		default:
			break;
		}
	}

	void Then(const OnResolveFunc& resolve, const OnRejectFunc& reject)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_resolveHandlers.push_back(resolve);
		m_rejectHandlers.push_back(reject);

		switch (m_state)
		{
		case State::Pending:
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

	void Then(const OnResolveFunc& resolve, const OnRejectFunc& reject, const OnProgressFunc& progress)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_resolveHandlers.push_back(resolve);
		m_rejectHandlers.push_back(reject);
		m_progressHandlers.push_back(progress);

		switch (m_state)
		{
		case State::Pending:
			break;
		case State::Resolved:
			progress(100);
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
		std::atomic<bool> resolved(false);
		std::atomic<bool> ok(true);

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

		size_t resolveIndex = 0;
		size_t rejectIndex = 0;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			resolveIndex = m_resolveHandlers.size() - 1;
			rejectIndex = m_rejectHandlers.size() - 1;
		}

		while (!resolved) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_resolveHandlers.erase(m_resolveHandlers.begin() + resolveIndex);
			m_rejectHandlers.erase(m_rejectHandlers.begin() + rejectIndex);
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

		for (const auto& cb : m_progressHandlers) {
			cb(100);
		}

		for (const auto& cb : m_resolveHandlers) {
			cb(m_result);
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
		for (const auto& cb : m_rejectHandlers) {
			cb(m_error);
		}

		PopPull(m_id);
	}

	void Progress(int progress)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if (m_state != State::Pending) {
			return;
		}

		for (const auto& cb : m_progressHandlers) {
			cb(progress);
		}
	}

	PromiseFunc m_impl;
	TResult m_result;
	TError m_error;
	std::vector<OnResolveFunc> m_resolveHandlers;
	std::vector<OnRejectFunc> m_rejectHandlers;
	std::vector<OnProgressFunc> m_progressHandlers;
	std::mutex m_mutex;
};

template<typename TResult>
class Promise : public TPromise<TResult>
{
private:
	class Async : public TPromise<TResult>
	{
	public:
		Async(const PromiseFunc& impl) : TPromise<TResult>(impl)
		{
		}

		virtual void Reset() override
		{
			if (m_threadPtr) {
				m_threadPtr->join();
			}

			m_state = State::Pending;

			m_threadPtr.reset(new std::thread(m_impl,
				[this](const TResult& result) { Resolve(result); },
				[this](const TError& error) { Reject(error); },
				[this](int progress) { Progress(progress); }
			));
		}

		~Async()
		{
			if (m_threadPtr) {
				m_threadPtr->join();
			}
		}
	private:
		std::shared_ptr<std::thread> m_threadPtr;
	};

public:
	virtual void Reset() override
	{
		m_state = State::Pending;

		m_impl(
			[this](const TResult& result) { Resolve(result); },
			[this](const TError& error) { Reject(error); },
			[this](int progress) { Progress(progress); }
		);
	}

	static TPromisePtr Create(const PromiseFunc& impl)
	{
		Promise<TResult>::TPromisePtr ptr(new Promise(impl));
		PushPull(ptr);
		return ptr;
	}

	static TPromisePtr CreateAsync(const PromiseFunc& impl)
	{
		TPromise<TResult>::TPromisePtr ptr(new Async(impl));
		PushPull(ptr);
		return ptr;
	}

	static std::shared_ptr<TPromise<std::vector<TResult>>> All(const std::vector<TPromise<TResult>::TPromisePtr>& all)
	{
		return Promise<std::vector<TResult>>::CreateAsync(
			[all](
				const Promise<std::vector<TResult>>::OnResolveFunc& resolve,
				const Promise<std::vector<TResult>>::OnRejectFunc& reject,
				const Promise<std::vector<TResult>>::OnProgressFunc& progress
			) {
				std::vector<TResult> result(all.size());
				for (size_t i = 0; i < all.size(); ++i) {
					TResult res;
					TError err;
					if (all[i]->Result(res, err)) {
						result[i] = res;
						progress(int(i * 100.0 / all.size()));
					}
					else {
						reject(err);
						return;
					}
				}

				resolve(result);
			}
		);
	}

private:
	Promise(const PromiseFunc& impl) : TPromise(impl)
	{}
};

