#include <iostream>
#include <thread>
#include <chrono>

#include "Promise.h"

Promise<std::string>::PromisePtr GetPromise(std::thread& t)
{
	return Promise<std::string>::Create(
		[&t](const Promise<std::string>::OnResolveFunc& resolve, const Promise<std::string>::OnRejectFunc& reject)
		{
			Promise<std::string>::PromiseFunc func = [](Promise<std::string>::OnResolveFunc resolve, Promise<std::string>::OnRejectFunc reject) {
				std::this_thread::sleep_for(std::chrono::seconds(1));
				resolve("resolved");
			};
			t = std::thread(func, resolve, reject);
		}
	);
}

int main()
{
	std::cout << "Hello World! " << std::this_thread::get_id() << "\n";

	std::thread t;

	auto p = GetPromise(t);

	p->Then(
		[](const std::string& result) {
			std::cout << result << " "  << std::this_thread::get_id() << "\n";
		},
		[](const std::string& error) {
			std::cout << error << "\n";
		}
	);

	t.join();

	p->Then(
		[](const std::string& result) {
			std::cout << result << " " << std::this_thread::get_id() << "\n";
		},
		[](const std::string& error) {
			std::cout << error << "\n";
		}
	);
}
