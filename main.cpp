#include <iostream>
#include <chrono>

#include "Promise.h"

int main()
{
	std::cout << "Hello World! main thread id " << std::this_thread::get_id() << "\n";

	auto async = AsyncPromise<std::string>::Create([](const Promise<std::string>::OnResolveFunc& resolve, const Promise<std::string>::OnRejectFunc& reject) {
		std::this_thread::sleep_for(std::chrono::seconds(2));
		resolve("async resolved");
	});

	async->Then(
		[](const std::string& result) {
			std::cout << result << " in thread id " << std::this_thread::get_id() << "\n";
		},
			[](const std::string& error) {
			std::cout << error << "\n";
		}
	);

	auto sync = SyncPromise<std::string>::Create([](const Promise<std::string>::OnResolveFunc& resolve, const Promise<std::string>::OnRejectFunc& reject) {
		resolve("sync resolved");
	});

	sync->Then(
		[](const std::string& result) {
			std::cout << result << " in thread id " << std::this_thread::get_id() << "\n";
		},
			[](const std::string& error) {
			std::cout << error << "\n";
		}
	);
}
