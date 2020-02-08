#include "Promise.h"

std::mutex IPromise::ms_poolMutex;
std::map<size_t, std::shared_ptr<IPromise>> IPromise::ms_pool;
std::condition_variable IPromise::ms_exitCondition;
size_t IPromise::ms_handlerId;
