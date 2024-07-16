#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <asio.hpp>
#include <asio/steady_timer.hpp>
#include <asio/bind_executor.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>

using namespace asio;
using namespace std::chrono_literals;

std::queue<int> num_q1, num_p2;
bool is_starting = true;

awaitable<void> producer(io_context::strand& strand1, steady_timer& timer1, steady_timer& timer2) {
    for (int i = 0; i < 10; i++) {
        for (size_t j = 0; j < 1000000; j++) {
            if ((i + 1) * j == (i + 1) * 100000) {
                num_q1.push((i + 1) * 100000);
                std::cout << "producer push" << std::endl;
                timer2.cancel();  // Notify consumer
                std::cout << "producer timer2 cancel" << std::endl;

                // Wait for the consumer to process
                std::error_code ec;
                co_await timer1.async_wait(redirect_error(use_awaitable, ec));
                std::cout << "producer wait_end" << std::endl;
            }
        }
    }
    is_starting = false;
    timer2.cancel();  // Notify consumer to exit loop
    std::cout << "producer routine_end1" << std::endl;
    co_return;
}

awaitable<void> consumer(io_context::strand& strand2, steady_timer& timer1, steady_timer& timer2) {
    while (is_starting || !num_q1.empty()) {
        std::error_code ec;
        co_await timer2.async_wait(redirect_error(use_awaitable, ec));
        std::cout << "consumer wait_end" << std::endl;

        if (!num_q1.empty()) {
            num_p2.push(num_q1.front());
            num_q1.pop();
            std::cout << "consumer pop" << std::endl;
            timer1.cancel();  // Notify producer
            std::cout << "consumer timer1 cancel" << std::endl;
        }
    }
    std::cout << "consumer routine_end2" << std::endl;
    co_return;
}

int main() {
    io_context ioc1, ioc2, ioc3;
    io_context::strand strand1(ioc1), strand2(ioc2), strand3(ioc3);

    steady_timer timer1(strand1.context()), timer2(strand2.context());



    post(ioc3, [&]() {
        asio::co_spawn(strand1.context(), producer(strand1, timer1, timer2), detached);
        asio::co_spawn(strand2.context(), consumer(strand2, timer1, timer2), detached);
        
        });

    std::thread t1([&ioc1]() { ioc1.run(); });
    std::thread t2([&ioc2]() { ioc2.run(); });
    std::thread t3([&ioc3]() { ioc3.run(); });

    std::this_thread::sleep_for(std::chrono::seconds(3));
    ioc1.stop();
    ioc2.stop();
    ioc3.stop();
    t1.join();
    t2.join();
    t3.join();

    return 0;
}