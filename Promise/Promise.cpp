#include "Promise.h"

size_t IPromise::ms_handlerId = 0;

void PromiseContext::Join()
{
	std::mutex m;
	std::unique_lock<std::mutex> lk(m);
	m_exitCondition.wait(lk, [this] { return m_pool.empty(); });
}

void PromiseContext::PushPool(size_t id, const std::shared_ptr<IPromise>& ptr)
{
	assert(ptr);
	std::lock_guard<std::mutex> lock(m_poolMutex);
	m_pool.insert(std::make_pair(id, ptr));
}

void PromiseContext::PopPool(size_t id)
{
	std::lock_guard<std::mutex> lock(m_poolMutex);
	m_pool.erase(id);

	if (m_pool.empty()) {
		m_exitCondition.notify_one();
	}
}

IPromise::State IPromise::GetState() const
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	return m_state;
}

