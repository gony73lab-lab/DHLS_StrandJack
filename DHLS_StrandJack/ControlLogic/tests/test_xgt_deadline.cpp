#include "../Evo/XgbXgtTcpReadOnly.h"
#include <cassert>
#include <iostream>
#include <thread>
int main()
{
    WSADATA data{};
    assert(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(listener != INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    assert(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    assert(listen(listener, 1) == 0);
    int size = sizeof(address);
    assert(getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size) == 0);
    std::thread server([&] {
        SOCKET peer = accept(listener, nullptr, nullptr);
        assert(peer != INVALID_SOCKET);
        char request[512]{};
        recv(peer, request, sizeof(request), 0);
        for (int i = 0; i < 12; ++i)
        {
            char byte = 0;
            if (send(peer, &byte, 1, 0) <= 0)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        closesocket(peer);
    });
    {
        XgbXgtTcpReadOnly reader("127.0.0.1", ntohs(address.sin_port), 80);
        assert(reader.Connect());
        std::vector<std::uint16_t> words;
        auto before = std::chrono::steady_clock::now();
        assert(!reader.ReadWords(10, 1, words));
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before)
                .count();
        assert(elapsed >= 50 && elapsed < 220);
    }
    server.join();
    closesocket(listener);
    WSACleanup();
    std::cout << "PASS: XGT absolute read deadline rejects trickled incomplete response\n";
}
