#include <iostream>

#include <Promise.h>

void runAsync(int i, PromiseContext& promises)
{
	auto async = promises.CreateAsync<std::string, std::string>(
		[](const TPromise<std::string, std::string>::HandlerPtr& h) {
				for (int i = 0; i < 100; ++i) {
					if (h->IsCanceled()) {
						std::cout << "async canceled" << std::endl;
						return;
					}

					if (i % 10 == 0) {
						h->Progress(i);
					}

					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
				h->Resolve("async resolved");
		}
	);

	async->Then(
		[i](const std::string& result) {
			std::cout << result << " " << i << " in thread id " << std::this_thread::get_id() << std::endl;
		},
		[i](int progress) {
			std::cout << i << " async progress " << progress << "%" << std::endl;
		}
	);
}

int main()
{
	std::cout << "Hello World! main thread id " << std::this_thread::get_id() << std::endl;

	PromiseContext promises;

	std::vector<TPromise<std::string, std::string>::HandlerPtr> list;

	{
		auto p = promises.Create<std::string, std::string>(
			[&list](const TPromise<std::string, std::string>::HandlerPtr& h) {
				list.push_back(h);
			});

		p->Then([](const std::string& result) {
			std::cout << result << std::endl;
		});

		//p->Cancel();
	}

	runAsync(0, promises);

	std::thread t([&list]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));

		for (const auto& h : list) {
			if (h->IsCanceled()) {
				std::cout << "is canceled" << std::endl;
			}

			h->Resolve("ok");
		}
	});

	t.join();
}
