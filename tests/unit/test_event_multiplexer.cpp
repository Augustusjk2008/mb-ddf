/**
 * @file test_event_multiplexer.cpp
 * @brief EventMultiplexer 单元测试
 *
 * 使用 UDP 本地回环测试 epoll 事件多路复用功能。
 * 测试不依赖实际硬件，可在任何 Linux 系统上运行。
 */

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>

#include "MB_DDF/PhysicalLayer/EventMultiplexer.h"

using namespace MB_DDF::PhysicalLayer;

// UDP socket 辅助类
class UDPSocket {
public:
    UDPSocket() : fd_(-1) {}

    ~UDPSocket() { close(); }

    bool create() {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        return fd_ >= 0;
    }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool bind(uint16_t port) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        return ::bind(fd_, (sockaddr*)&addr, sizeof(addr)) == 0;
    }

    bool setNonBlocking() {
        int flags = fcntl(fd_, F_GETFL, 0);
        return fcntl(fd_, F_SETFL, flags | O_NONBLOCK) >= 0;
    }

    ssize_t sendTo(const void* data, size_t len, uint16_t port) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(port);
        return ::sendto(fd_, data, len, 0, (sockaddr*)&addr, sizeof(addr));
    }

    ssize_t recvFrom(void* buffer, size_t len) {
        sockaddr_in addr{};
        socklen_t addrLen = sizeof(addr);
        return ::recvfrom(fd_, buffer, len, 0, (sockaddr*)&addr, &addrLen);
    }

    int fd() const { return fd_; }

private:
    int fd_;
};

// ==============================
// EventMultiplexer 基础测试
// ==============================
TEST(EventMultiplexerTest, CreateAndDestroy) {
    {
        EventMultiplexer mux;
        EXPECT_GE(mux.epoll_fd(), 0);
    }
    SUCCEED();
}

TEST(EventMultiplexerTest, AddValidFd) {
    EventMultiplexer mux;
    UDPSocket sock;

    ASSERT_TRUE(sock.create());
    ASSERT_TRUE(sock.bind(0));  // 随机端口

    bool callbackCalled = false;
    EXPECT_TRUE(mux.add(sock.fd(), EPOLLIN, [&callbackCalled](int /*fd*/, uint32_t /*ev*/) {
        callbackCalled = true;
    }));

    EXPECT_FALSE(callbackCalled);  // 还没有事件
}

TEST(EventMultiplexerTest, AddInvalidFd) {
    EventMultiplexer mux;

    bool callbackCalled = false;
    EXPECT_FALSE(mux.add(-1, EPOLLIN, [&callbackCalled](int /*fd*/, uint32_t /*ev*/) {
        callbackCalled = true;
    }));
}

TEST(EventMultiplexerTest, RemoveFd) {
    EventMultiplexer mux;
    UDPSocket sock;

    ASSERT_TRUE(sock.create());
    ASSERT_TRUE(sock.bind(0));

    int fd = sock.fd();
    EXPECT_TRUE(mux.add(fd, EPOLLIN, [](int, uint32_t) {}));
    EXPECT_TRUE(mux.remove(fd));

    // 重复移除应该也是安全的
    EXPECT_TRUE(mux.remove(fd));
}

TEST(EventMultiplexerTest, RemoveInvalidFd) {
    EventMultiplexer mux;

    // 移除无效 fd 应该返回 false
    EXPECT_FALSE(mux.remove(-1));
}

// ==============================
// 事件等待测试
// ==============================
TEST(EventMultiplexerTest, WaitOnceTimeout) {
    EventMultiplexer mux;

    // 没有注册任何 fd，应该超时返回 0
    int n = mux.wait_once(100);  // 100ms 超时
    EXPECT_EQ(n, 0);
}

TEST(EventMultiplexerTest, WaitOnceWithEvent) {
    EventMultiplexer mux;
    UDPSocket recvSock;
    UDPSocket sendSock;

    ASSERT_TRUE(recvSock.create());
    ASSERT_TRUE(recvSock.bind(15001));  // 固定测试端口
    ASSERT_TRUE(recvSock.setNonBlocking());

    ASSERT_TRUE(sendSock.create());

    std::atomic<bool> eventReceived{false};
    std::atomic<uint32_t> receivedEvents{0};

    EXPECT_TRUE(mux.add(recvSock.fd(), EPOLLIN, [&](int /*fd*/, uint32_t ev) {
        eventReceived = true;
        receivedEvents = ev;
    }));

    // 发送数据触发事件
    const char* msg = "Hello";
    EXPECT_EQ(sendSock.sendTo(msg, strlen(msg), 15001), (ssize_t)strlen(msg));

    // 等待事件
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int n = mux.wait_once(500);

    EXPECT_EQ(n, 1);
    EXPECT_TRUE(eventReceived);
    EXPECT_TRUE(receivedEvents & EPOLLIN);
}

TEST(EventMultiplexerTest, WaitOnceMultipleEvents) {
    EventMultiplexer mux;
    UDPSocket sock1, sock2;
    UDPSocket sendSock;

    ASSERT_TRUE(sock1.create());
    ASSERT_TRUE(sock1.bind(15002));
    ASSERT_TRUE(sock1.setNonBlocking());

    ASSERT_TRUE(sock2.create());
    ASSERT_TRUE(sock2.bind(15003));
    ASSERT_TRUE(sock2.setNonBlocking());

    ASSERT_TRUE(sendSock.create());

    std::atomic<int> eventCount{0};

    EXPECT_TRUE(mux.add(sock1.fd(), EPOLLIN, [&](int, uint32_t) { eventCount++; }));
    EXPECT_TRUE(mux.add(sock2.fd(), EPOLLIN, [&](int, uint32_t) { eventCount++; }));

    // 向两个 socket 发送数据
    sendSock.sendTo("A", 1, 15002);
    sendSock.sendTo("B", 1, 15003);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int n = mux.wait_once(500);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(eventCount, 2);
}

// ==============================
// 数据读取测试
// ==============================
TEST(EventMultiplexerTest, ReadDataInCallback) {
    EventMultiplexer mux;
    UDPSocket recvSock;
    UDPSocket sendSock;

    ASSERT_TRUE(recvSock.create());
    ASSERT_TRUE(recvSock.bind(15004));
    ASSERT_TRUE(recvSock.setNonBlocking());

    ASSERT_TRUE(sendSock.create());

    char receivedBuffer[64] = {0};
    ssize_t receivedLen = 0;

    EXPECT_TRUE(mux.add(recvSock.fd(), EPOLLIN, [&](int /*fd*/, uint32_t /*ev*/) {
        receivedLen = recvSock.recvFrom(receivedBuffer, sizeof(receivedBuffer));
    }));

    const char* msg = "Test Message";
    sendSock.sendTo(msg, strlen(msg), 15004);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    mux.wait_once(500);

    EXPECT_EQ(receivedLen, (ssize_t)strlen(msg));
    EXPECT_STREQ(receivedBuffer, msg);
}

// ==============================
// 循环运行测试
// ==============================
TEST(EventMultiplexerTest, RunLoopAndStop) {
    EventMultiplexer mux;
    UDPSocket sock;

    ASSERT_TRUE(sock.create());
    ASSERT_TRUE(sock.bind(15005));
    ASSERT_TRUE(sock.setNonBlocking());

    std::atomic<int> eventCount{0};

    EXPECT_TRUE(mux.add(sock.fd(), EPOLLIN, [&](int, uint32_t) { eventCount++; }));

    // 在另一个线程运行 loop
    std::thread loopThread([&]() {
        mux.run_loop(100);  // 100ms 超时
    });

    UDPSocket sendSock;
    ASSERT_TRUE(sendSock.create());

    // 发送几次数据
    for (int i = 0; i < 3; ++i) {
        sendSock.sendTo("X", 1, 15005);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    // 停止 loop
    mux.stop();
    loopThread.join();

    EXPECT_GE(eventCount, 3);
}

// ==============================
// 更新注册测试
// ==============================
TEST(EventMultiplexerTest, UpdateExistingFd) {
    EventMultiplexer mux;
    UDPSocket sock;

    ASSERT_TRUE(sock.create());
    ASSERT_TRUE(sock.bind(15006));

    int callCount1 = 0;
    int callCount2 = 0;

    // 第一次注册
    EXPECT_TRUE(mux.add(sock.fd(), EPOLLIN, [&](int, uint32_t) { callCount1++; }));

    // 第二次注册（更新）
    EXPECT_TRUE(mux.add(sock.fd(), EPOLLIN, [&](int, uint32_t) { callCount2++; }));

    // 只应该调用第二个回调
    UDPSocket sendSock;
    ASSERT_TRUE(sendSock.create());
    sendSock.sendTo("X", 1, 15006);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    mux.wait_once(500);

    EXPECT_EQ(callCount1, 0);
    EXPECT_EQ(callCount2, 1);
}

// ==============================
// 错误处理测试
// ==============================
TEST(EventMultiplexerTest, EmptyCallback) {
    EventMultiplexer mux;
    UDPSocket sock;

    ASSERT_TRUE(sock.create());
    ASSERT_TRUE(sock.bind(15007));
    ASSERT_TRUE(sock.setNonBlocking());

    // 空回调不应该崩溃
    EXPECT_TRUE(mux.add(sock.fd(), EPOLLIN, nullptr));

    UDPSocket sendSock;
    ASSERT_TRUE(sendSock.create());
    sendSock.sendTo("X", 1, 15007);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 不应该崩溃
    int n = mux.wait_once(500);
    EXPECT_EQ(n, 1);
}

// ==============================
// 综合场景测试
// ==============================
TEST(EventMultiplexerTest, MultiClientServer) {
    // 模拟服务器处理多个客户端
    EventMultiplexer mux;
    UDPSocket serverSock;

    ASSERT_TRUE(serverSock.create());
    ASSERT_TRUE(serverSock.bind(15010));
    ASSERT_TRUE(serverSock.setNonBlocking());

    std::vector<std::string> receivedMessages;

    EXPECT_TRUE(mux.add(serverSock.fd(), EPOLLIN, [&](int /*fd*/, uint32_t /*ev*/) {
        char buf[256];
        ssize_t n = serverSock.recvFrom(buf, sizeof(buf));
        if (n > 0) {
            buf[n] = '\0';
            receivedMessages.push_back(buf);
        }
    }));

    // 多个客户端发送数据
    UDPSocket client1, client2, client3;
    ASSERT_TRUE(client1.create());
    ASSERT_TRUE(client2.create());
    ASSERT_TRUE(client3.create());

    client1.sendTo("Client1", 7, 15010);
    client2.sendTo("Client2", 7, 15010);
    client3.sendTo("Client3", 7, 15010);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 处理所有事件
    while (mux.wait_once(100) > 0) {
        // 继续处理直到超时
    }

    EXPECT_EQ(receivedMessages.size(), 3);
}
