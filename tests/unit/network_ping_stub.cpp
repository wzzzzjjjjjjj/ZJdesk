#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

int main()
{
    if (const auto *addresses = std::getenv("ZJDESK_TEST_SSH_ADDRESSES"))
    {
        std::cout << addresses;
        return 0;
    }
    const auto *environment = std::getenv("ZJDESK_TEST_PING");
    const std::string mode = environment ? environment : "ok";
    if (mode == "slow") std::this_thread::sleep_for(std::chrono::seconds(30));
    if (mode == "fail") return 1;
    if (mode != "unreachable") std::cout << "Reply: bytes=32 time=1ms TTL=64\n";
    else std::cout << "Destination host unreachable.\n";
    return 0;
}
