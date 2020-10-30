#pragma once

#include <cassert>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

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

	State GetState() const;

protected:
	State m_state;
	static size_t ms_handlerId;
	mutable std::recursive_mutex m_mutex;

	IPromise(): m_state(State::Pending)
	{
	}

};

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

	bool Result(TResult& result, uint32_t timeoutMs = 0)
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
				[&resolved, &cv](const TError&) {
					resolved = true;
					cv.notify_one();
				}
			);

			resolveIndex = m_resolveHandlers.crbegin()->first;
		}

		std::mutex m;
		std::unique_lock<std::mutex> lk(m);

		if (timeoutMs > 0) {
			cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&resolved, this] { return resolved == true || GetState() == State::Canceled; });
		}
		else {
			cv.wait(lk, [&resolved, this] { return resolved == true || GetState() == State::Canceled; });
		}

		{
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_cancelConditionPtr = nullptr;
			m_resolveHandlers.erase(resolveIndex);
		}

		return ok;
	}

	bool Result(TResult& result, const OnProgressFunc& progress, uint32_t timeoutMs = 0)
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
					cv.notify_one();
				},
				[&resolved, &cv](const std::string&) {
					resolved = true;
					cv.notify_one();
				}, 
				[&progress](int p) {
					progress(p);
				}
			);

			resolveIndex = m_resolveHandlers.crbegin()->first;
			progressIndex = m_progressHandlers.crbegin()->first;
		}

		std::mutex m;
		std::unique_lock<std::mutex> lk(m);

		if (timeoutMs > 0) {
			cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&resolved, this] { return resolved == true || GetState() == State::Canceled; });
		}
		else {
			cv.wait(lk, [&resolved, this] { return resolved == true || GetState() == State::Canceled; });
		}

		{
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_cancelConditionPtr = nullptr;
			m_resolveHandlers.erase(resolveIndex);
			m_progressHandlers.erase(progressIndex);
		}

		return ok;
	}

	bool Result(TResult& result, TError& error, uint32_t timeoutMs = 0)
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

		if (timeoutMs > 0) {
			cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&resolved, this] { return resolved == true || GetState() == State::Canceled; });
		}
		else {
			cv.wait(lk, [&resolved, this] { return resolved == true || GetState() == State::Canceled; });
		}

		{
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_cancelConditionPtr = nullptr;
			m_resolveHandlers.erase(resolveIndex);
			m_rejectHandlers.erase(rejectIndex);
		}

		return ok;
	}

	bool Result(TResult& result, TError& error, const OnProgressFunc& progress, uint32_t timeoutMs = 0)
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
					cv.notify_one();
				},
				[&error, &resolved, &cv](const TError& value) {
					error = value;
					resolved = true;
					cv.notify_one();
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

		if (timeoutMs > 0) {
			cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&resolved, this] { return resolved == true || GetState() == State::Canceled; });
		}
		else {
			cv.wait(lk, [&resolved, this] { return resolved == true || GetState() == State::Canceled; });
		}

		{
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_cancelConditionPtr = nullptr;
			m_resolveHandlers.erase(resolveIndex);
			m_rejectHandlers.erase(rejectIndex);
			m_progressHandlers.erase(progressIndex);
		}

		return ok;
	}

	virtual void Cancel()
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);

		if (m_state != State::Pending) {
			return;
		}

		m_state = State::Canceled;

		if (m_cancelConditionPtr != nullptr) {
			m_cancelConditionPtr->notify_one();
		}
	}

protected:
	TPromise(const PromiseFunc& impl) : IPromise(), m_impl(impl), m_cancelConditionPtr(nullptr)
	{
	}

	virtual void Resolve(const TResult& result)
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
	}

	virtual void Reject(const TError& error)
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
	}

	bool IsCanceled() 
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		return m_state == State::Canceled;
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

class PromiseContext
{
private:
	template<typename TResult>
	class PromiseBase : public TPromise<TResult>
	{
	public:
		size_t GetId() const { return m_id; }

		virtual void Cancel() override
		{
			TPromise<TResult>::Cancel();
			m_context.PopPool(m_id);
		}

	protected:
		PromiseBase(const typename TPromise<TResult>::PromiseFunc& impl, PromiseContext& context) : TPromise<TResult>(impl), m_context(context) {
			std::lock_guard<std::mutex> lock(context.m_poolMutex);
			static size_t lastId = 0;
			m_id = lastId++;
		}

		virtual void Resolve(const TResult& result) override
		{
			TPromise<TResult>::Resolve(result);
			m_context.PopPool(m_id);
		}

		virtual void Reject(const typename TPromise<TResult>::TError& error) override
		{
			TPromise<TResult>::Reject(error);
			m_context.PopPool(m_id);
		}

	private:
		size_t m_id;
		PromiseContext& m_context;
	};

	template<typename TResult>
	class Promise : public PromiseBase<TResult>
	{
	public:
		Promise(const typename TPromise<TResult>::PromiseFunc& impl, PromiseContext& context) : PromiseBase<TResult>(impl, context)
		{}

		virtual void Reset() override
		{
			IPromise::m_state = IPromise::State::Pending;

			TPromise<TResult>::Run(
				[this](const TResult& result) { PromiseBase<TResult>::Resolve(result); },
				[this](const typename TPromise<TResult>::TError& error) { PromiseBase<TResult>::Reject(error); },
				[this](int progress) { PromiseBase<TResult>::Progress(progress); },
				[this]() { return PromiseBase<TResult>::IsCanceled(); }
			);
		}
	};

	template<typename TResult>
	class AsyncPromise : public PromiseBase<TResult>
	{
	public:
		AsyncPromise(const typename TPromise<TResult>::PromiseFunc& impl, PromiseContext& context) : PromiseBase<TResult>(impl, context)
		{
		}

		virtual void Reset() override
		{
			if (m_threadPtr) {
				m_threadPtr->join();
			}

			IPromise::m_state = IPromise::State::Pending;

			m_threadPtr.reset(new std::thread(
				[this](
					const typename TPromise<TResult>::OnResolveFunc& resolve,
					const typename TPromise<TResult>::OnRejectFunc& reject,
					const typename TPromise<TResult>::OnProgressFunc& progress,
					const typename TPromise<TResult>::IsCanceledFunc& isCanceled
					) {
						TPromise<TResult>::Run(resolve, reject, progress, isCanceled);
					},
					[this](const TResult& result) { PromiseBase<TResult>::Resolve(result); },
						[this](const typename TPromise<TResult>::TError& error) { PromiseBase<TResult>::Reject(error); },
						[this](int progress) { PromiseBase<TResult>::Progress(progress); },
						[this]() { return PromiseBase<TResult>::IsCanceled(); }
					));
		}

		~AsyncPromise()
		{
			if (m_threadPtr) {
				m_threadPtr->join();
			}
		}

	private:
		std::shared_ptr<std::thread> m_threadPtr;
	};

public:
	~PromiseContext();

	template<typename TResult>
	std::shared_ptr<TPromise<TResult>> Create(const std::function<void(
		const std::function<void(const TResult & result)> & resolve,
		const std::function<void(const std::string & error)> & reject,
		const std::function<void(int progress)> & progress,
		const std::function<bool()> & isCanceled
		)>& impl)
	{
		Promise<TResult>* p = new Promise<TResult>(impl, *this);
		std::shared_ptr<Promise<TResult>> ptr(p);
		PushPool(p->GetId(), ptr);
		ptr->Reset();
		return ptr;
	}

	template<typename TResult>
	std::shared_ptr<TPromise<TResult>> CreateAsync(const std::function<void(
		const std::function<void(const TResult & result)> & resolve,
		const std::function<void(const std::string & error)> & reject,
		const std::function<void(int progress)> & progress,
		const std::function<bool()>
		)> & impl)
	{
		AsyncPromise<TResult>* p = new AsyncPromise<TResult>(impl, *this);
		std::shared_ptr<TPromise<TResult>> ptr(p);
		PushPool(p->GetId(), ptr);
		ptr->Reset();
		return ptr;
	}

	template<typename TResult>
	std::shared_ptr<TPromise<std::vector<TResult>>>  All(const std::vector<std::shared_ptr<TPromise<TResult>>>& all)
	{
		return CreateAsync<std::vector<TResult>>(
			[all](
				const typename Promise<std::vector<TResult>>::OnResolveFunc& resolve,
				const typename Promise<std::vector<TResult>>::OnRejectFunc& reject,
				const typename Promise<std::vector<TResult>>::OnProgressFunc& progress,
				const typename Promise<std::string>::IsCanceledFunc& isCanceled
				) {
					std::vector<TResult> result(all.size());
					for (size_t i = 0; i < all.size(); ++i) {

						if (isCanceled()) {
							return;
						}

						TResult res;
						std::string err;
						if (all[i]->Result(res, err)) {
							result[i] = res;
							progress(int(i * 100.0 / all.size()));
						}
						else {
							if (all[i]->GetState() == IPromise::State::Rejected) {
								reject(err);
							}
							return;
						}
					}

					resolve(result);
			}
		);
	}

	void Join();

private:
	void PushPool(size_t id, const std::shared_ptr<IPromise>& ptr);
	void PopPool(size_t id);

	std::mutex m_poolMutex;
	std::map<size_t, std::shared_ptr<IPromise>> m_pool;
	std::condition_variable m_exitCondition;
};
