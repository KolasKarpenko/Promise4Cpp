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
	enum class State
	{
		Pending = 0,
		Resolved,
		Rejected,
		Canceled
	};

	virtual ~IPromise() {}
	virtual void Reset() = 0;

	State GetState() const {
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		return m_state;
	}

	static void Join() {
		std::mutex m;
		std::unique_lock<std::mutex> lk(m);
		ms_exitCondition.wait(lk, [] { return ms_pool.empty(); });
	}

protected:
	size_t m_id;
	State m_state;
	static size_t ms_handlerId;
	mutable std::recursive_mutex m_mutex;

	IPromise(): m_state(State::Pending)
	{
		static size_t lastId = 0;
		std::lock_guard<std::mutex> lock(ms_poolMutex);
		m_id = lastId++;
	}

	static void PushPool(const std::shared_ptr<IPromise>& ptr)
	{
		assert(ptr);
		std::lock_guard<std::mutex> lock(ms_poolMutex);
		ms_pool.insert(std::make_pair(ptr->m_id, ptr));
	}

	static void PopPool(size_t id)
	{
		std::lock_guard<std::mutex> lock(ms_poolMutex);
		ms_pool.erase(id);

		if (ms_pool.empty()) {
			ms_exitCondition.notify_one();
		}
	}

private:
	static std::mutex ms_poolMutex;
	static std::map<size_t, std::shared_ptr<IPromise>> ms_pool;
	static std::condition_variable ms_exitCondition;
};

std::mutex IPromise::ms_poolMutex;
std::map<size_t, std::shared_ptr<IPromise>> IPromise::ms_pool;
std::condition_variable IPromise::ms_exitCondition;
size_t IPromise::ms_handlerId;

template<typename TResult>
class TPromise : public IPromise
{
public:
	typedef std::string TError;
	typedef std::function<void(const TResult& result)> OnResolveFunc;
	typedef std::function<void(const TError& error)> OnRejectFunc;
	typedef std::function<void(int progress)> OnProgressFunc;
	typedef std::function<bool()> IsCanceledFunc;
	typedef std::function<void(
		const OnResolveFunc& resolve,
		const OnRejectFunc& reject,
		const OnProgressFunc& progress,
		const IsCanceledFunc& isCanceled
	)> PromiseFunc;
	typedef std::shared_ptr<TPromise<TResult>> PromisePtr;

	void Then(const OnResolveFunc& resolve)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		m_resolveHandlers.insert(std::make_pair(ms_handlerId++, resolve));

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
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		m_resolveHandlers.insert(std::make_pair(ms_handlerId++, resolve));
		m_progressHandlers.insert(std::make_pair(ms_handlerId++, progress));

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
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		m_resolveHandlers.insert(std::make_pair(ms_handlerId++, resolve));
		m_rejectHandlers.insert(std::make_pair(ms_handlerId++, reject));

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
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		m_resolveHandlers.insert(std::make_pair(ms_handlerId++, resolve));
		m_rejectHandlers.insert(std::make_pair(ms_handlerId++, reject));
		m_progressHandlers.insert(std::make_pair(ms_handlerId++, progress));

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

	bool Result(TResult& result)
	{
		std::condition_variable cv;
		std::atomic<bool> resolved(false);
		std::atomic<bool> ok(false);

		size_t resolveIndex = 0;

		{
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_cancelConditionPtr = &cv;

			Then(
				[&result, &resolved, &ok, &cv](const TResult& value) {
					result = value;
					resolved = true;
					ok = true;
					cv.notify_one();
				},
				[&resolved, &cv](const TError& /*value*/) {
					resolved = true;
					cv.notify_one();
				}
			);

			resolveIndex = m_resolveHandlers.crbegin()->first;
		}

		std::mutex m;
		std::unique_lock<std::mutex> lk(m);
		cv.wait(lk, [&resolved, this] { 
			return resolved == true || GetState() == State::Canceled; 
		});

		{
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_cancelConditionPtr = nullptr;
			m_resolveHandlers.erase(resolveIndex);
		}

		return ok;
	}

	bool Result(TResult& result, TError& error)
	{
		std::condition_variable cv;
		std::atomic<bool> resolved(false);
		std::atomic<bool> ok(false);

		size_t resolveIndex = 0;
		size_t rejectIndex = 0;

		{
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_cancelConditionPtr = &cv;

			Then(
				[&result, &resolved, &ok, &cv](const TResult& value) {
					result = value;
					resolved = true;
					ok = true;
					cv.notify_one();
				},
				[&error, &resolved, &cv](const TError& value) {
					error = value;
					resolved = true;
					cv.notify_one();
				}
			);

			resolveIndex = m_resolveHandlers.crbegin()->first;
			rejectIndex = m_rejectHandlers.crbegin()->first;
		}

		std::mutex m;
		std::unique_lock<std::mutex> lk(m);
		cv.wait(lk, [&resolved, this] { return resolved == true || GetState() == State::Canceled; });

		{
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_cancelConditionPtr = nullptr;
			m_resolveHandlers.erase(resolveIndex);
			m_rejectHandlers.erase(rejectIndex);
		}

		return ok;
	}

	bool Result(TResult& result, TError& error, const OnProgressFunc& progress)
	{
		std::condition_variable cv;
		std::atomic<bool> resolved(false);
		std::atomic<bool> ok(false);

		size_t resolveIndex = 0;
		size_t rejectIndex = 0;
		size_t progressIndex = 0;

		{
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_cancelConditionPtr = &cv;

			Then(
				[&result, &resolved, &ok, &cv](const TResult& value) {
					result = value;
					resolved = true;
					ok = true;
				},
				[&error, &resolved, &cv](const TError& value) {
					error = value;
					resolved = true;
				},
				[&progress](int p) {
					progress(p);
				}
			);

			resolveIndex = m_resolveHandlers.crbegin()->first;
			rejectIndex = m_rejectHandlers.crbegin()->first;
			progressIndex = m_progressHandlers.crbegin()->first;
		}

		std::mutex m;
		std::unique_lock<std::mutex> lk(m);
		cv.wait(lk, [&resolved, this] { return resolved == true || GetState() == State::Canceled; });

		{
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_cancelConditionPtr = nullptr;
			m_resolveHandlers.erase(resolveIndex);
			m_rejectHandlers.erase(rejectIndex);
			m_progressHandlers.erase(progressIndex);
		}

		return ok;
	}

	void Cancel()
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);

		if (m_state != State::Pending) {
			return;
		}

		m_state = State::Canceled;

		PopPool(m_id);

		if (m_cancelConditionPtr != nullptr) {
			m_cancelConditionPtr->notify_one();
		}
	}

protected:
	TPromise(const PromiseFunc& impl) : IPromise(), m_impl(impl), m_cancelConditionPtr(nullptr)
	{
	}

	void Resolve(const TResult& result)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);

		if (m_state != State::Pending) {
			return;
		}

		m_state = State::Resolved;
		m_result = result;

		for (const auto& cb : m_progressHandlers) {
			cb.second(100);
		}

		for (const auto& cb : m_resolveHandlers) {
			cb.second(m_result);
		}

		PopPool(m_id);
	}

	void Reject(const TError& error)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);

		if (m_state != State::Pending) {
			return;
		}

		m_state = State::Rejected;
		m_error = error;
		for (const auto& cb : m_rejectHandlers) {
			cb.second(m_error);
		}

		PopPool(m_id);
	}

	void Progress(int progress)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);

		if (m_state != State::Pending) {
			return;
		}

		for (const auto& cb : m_progressHandlers) {
			cb.second(progress);
		}
	}

	void Run(
		const OnResolveFunc& resolve,
		const OnRejectFunc& reject,
		const OnProgressFunc& progress,
		const IsCanceledFunc& isCanceled
	) {
		m_impl(resolve, reject, progress, isCanceled);

		if (GetState() == State::Pending) {
			Cancel();
		}
	}

private:
	PromiseFunc m_impl;
	TResult m_result;
	TError m_error;
	std::map<size_t, OnResolveFunc> m_resolveHandlers;
	std::map<size_t, OnRejectFunc> m_rejectHandlers;
	std::map<size_t, OnProgressFunc> m_progressHandlers;
	std::condition_variable* m_cancelConditionPtr;
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

			m_threadPtr.reset(new std::thread(
				[this](
					const TPromise<TResult>::OnResolveFunc& resolve,
					const TPromise<TResult>::OnRejectFunc& reject,
					const TPromise<TResult>::OnProgressFunc& progress,
					const TPromise<TResult>::IsCanceledFunc& isCanceled
				) {
					Run(resolve, reject, progress, isCanceled);
				},
				[this](const TResult& result) { Resolve(result); },
				[this](const TError& error) { Reject(error); },
				[this](int progress) { Progress(progress); },
				[this]() { return GetState() == State::Canceled; }
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

		Run(
			[this](const TResult& result) { Resolve(result); },
			[this](const TError& error) { Reject(error); },
			[this](int progress) { Progress(progress); },
			[this]() { return GetState() == State::Canceled; }
		);
	}

	static PromisePtr Create(const PromiseFunc& impl)
	{
		Promise<TResult>::PromisePtr ptr(new Promise(impl));
		PushPool(ptr);
		ptr->Reset();
		return ptr;
	}

	static PromisePtr CreateAsync(const PromiseFunc& impl)
	{
		TPromise<TResult>::PromisePtr ptr(new Async(impl));
		PushPool(ptr);
		ptr->Reset();
		return ptr;
	}

	static std::shared_ptr<TPromise<std::vector<TResult>>> All(const std::vector<TPromise<TResult>::PromisePtr>& all)
	{
		return Promise<std::vector<TResult>>::CreateAsync(
			[all](
				const Promise<std::vector<TResult>>::OnResolveFunc& resolve,
				const Promise<std::vector<TResult>>::OnRejectFunc& reject,
				const Promise<std::vector<TResult>>::OnProgressFunc& progress,
				const Promise<std::string>::IsCanceledFunc& isCanceled
			) {
				std::vector<TResult> result(all.size());
				for (size_t i = 0; i < all.size(); ++i) {

					if (isCanceled()) {
						return;
					}

					TResult res;
					TError err;
					if (all[i]->Result(res, err)) {
						result[i] = res;
						progress(int(i * 100.0 / all.size()));
					}
					else {
						if (all[i]->GetState() == State::Rejected) {
							reject(err);
						}
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

