#include <iostream>

#include "Promise.h"

int main()
{
	std::cout << "Hello World! main thread id " << std::this_thread::get_id() << std::endl;

	auto async = Promise<std::string>::CreateAsync(
		[](
			const Promise<std::string>::OnResolveFunc& resolve,
			const Promise<std::string>::OnRejectFunc& reject,
			const Promise<std::string>::OnProgressFunc& progress,
			const Promise<std::string>::IsCanceledFunc& isCanceled
		) {
			for (int i = 0; i < 10; ++i) {
				if (isCanceled()) {
					std::cout << "async canceled" << std::endl;
					return;
				}
				progress(i * 10);
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			resolve("async resolved");
		}
	);

	async->Then(
		[](const std::string& result) {
			std::cout << result << " in thread id " << std::this_thread::get_id() << std::endl;
		},
		[](int progress) {
			std::cout << "async progress " << progress << "%" << std::endl;
		}
	);

	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	async->Cancel();

	auto sync = Promise<std::string>::Create(
		[](
			const Promise<std::string>::OnResolveFunc& resolve,
			const Promise<std::string>::OnRejectFunc& reject,
			const Promise<std::string>::OnProgressFunc& progress,
			const Promise<std::string>::IsCanceledFunc& isCanceled
		) {
			//reject("error");
			resolve("sync resolved");
		}
	);

	sync->Then(
		[](const std::string& result) {
			std::cout << result << " in thread id " << std::this_thread::get_id() << std::endl;
		},
		[](int progress) {
			std::cout << "sync progress " << progress << "%" << std::endl;
		}
	);

	auto all = Promise<std::string>::All({ sync, async });

	all->Then(
		[](const std::vector<std::string>& result) {
			for (const auto& r : result) {
				std::cout << "all " << r << " in thread id " << std::this_thread::get_id() << std::endl;
			}
		},
		[](int progress) {
			std::cout << "all progress " << progress << "%" << std::endl;
		}
	);

	async->Reset();

	std::string res;
	if (async->Result(res)) {
		std::cout << "async->Result(res) " << res << std::endl;
	}

	IPromise::Join();
}
