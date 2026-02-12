/**
 * @file test_ringbuffer.cpp
 * @brief RingBuffer单元测试
 *
 * 测试无锁环形缓冲区的核心功能，包括：
 * - 基本发布/订阅
 * - 多订阅者隔离
 * - 环形回绕
 * - 零拷贝API
 * - 消息覆盖行为
 */

#include <gtest/gtest.h>
#include <semaphore.h>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <thread>
#include <atomic>

#include "MB_DDF/DDS/RingBuffer.h"
#include "MB_DDF/DDS/Message.h"

using namespace MB_DDF::DDS;

// ==============================
// 测试固件
// ==============================
class RingBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建进程内信号量
        sem_init(&sem_, 0, 1);

        // 分配对齐的64KB缓冲区（RingBuffer需要64字节对齐）
        size_t buffer_size = 64 * 1024;
        buffer_ = static_cast<uint8_t*>(aligned_alloc(64, buffer_size));
        ASSERT_NE(buffer_, nullptr);
        std::memset(buffer_, 0, buffer_size);

        // RingBuffer对象在栈上创建，管理独立的缓冲区
        rb_ = std::make_unique<RingBuffer>(buffer_, buffer_size, &sem_, true);
    }

    void TearDown() override {
        rb_.reset();
        free(buffer_);
        sem_destroy(&sem_);
    }

    sem_t sem_;
    uint8_t* buffer_;
    std::unique_ptr<RingBuffer> rb_;
};

// ==============================
// 基本发布/订阅
// ==============================
TEST_F(RingBufferTest, BasicPublishAndRead) {
    const char* data = "Hello, RingBuffer!";
    size_t data_size = strlen(data) + 1;

    // 发布消息
    EXPECT_TRUE(rb_->publish_message(data, data_size));

    // 注册订阅者
    auto* sub = rb_->register_subscriber(1, "test_sub");
    ASSERT_NE(sub, nullptr);

    // 读取消息
    Message* msg = nullptr;
    EXPECT_TRUE(rb_->read_next(sub, msg));
    ASSERT_NE(msg, nullptr);

    // 验证数据
    EXPECT_EQ(msg->header.data_size, data_size);
    EXPECT_STREQ(static_cast<const char*>(msg->get_data()), data);
}

TEST_F(RingBufferTest, MultipleMessages) {
    // 发布多条消息
    for (int i = 0; i < 10; ++i) {
        std::string msg = "Message " + std::to_string(i);
        EXPECT_TRUE(rb_->publish_message(msg.c_str(), msg.size() + 1));
    }

    auto* sub = rb_->register_subscriber(1, "seq_sub");

    // 按顺序读取
    for (int i = 0; i < 10; ++i) {
        Message* msg = nullptr;
        EXPECT_TRUE(rb_->read_next(sub, msg));

        std::string expected = "Message " + std::to_string(i);
        EXPECT_STREQ(static_cast<const char*>(msg->get_data()), expected.c_str());
    }
}

// ==============================
// 多订阅者隔离
// ==============================
TEST_F(RingBufferTest, MultiSubscriberIsolation) {
    const char* data = "Shared message";
    rb_->publish_message(data, strlen(data) + 1);

    // 注册两个订阅者
    auto* sub1 = rb_->register_subscriber(1, "sub1");
    auto* sub2 = rb_->register_subscriber(2, "sub2");

    // sub1读取
    Message* msg1 = nullptr;
    EXPECT_TRUE(rb_->read_next(sub1, msg1));
    EXPECT_STREQ(static_cast<const char*>(msg1->get_data()), data);

    // sub2仍可读取同一条消息
    Message* msg2 = nullptr;
    EXPECT_TRUE(rb_->read_next(sub2, msg2));
    EXPECT_STREQ(static_cast<const char*>(msg2->get_data()), data);

    // sub1再次读取无新消息
    Message* msg3 = nullptr;
    EXPECT_FALSE(rb_->read_next(sub1, msg3));
}

TEST_F(RingBufferTest, SubscriberIndependentPosition) {
    // 发布消息1
    rb_->publish_message("msg1", 5);

    auto* sub1 = rb_->register_subscriber(1, "fast_sub");
    auto* sub2 = rb_->register_subscriber(2, "slow_sub");

    // sub1读取msg1
    Message* msg = nullptr;
    EXPECT_TRUE(rb_->read_next(sub1, msg));

    // 发布消息2、3
    rb_->publish_message("msg2", 5);
    rb_->publish_message("msg3", 5);

    // sub1直接读到msg3（最新）
    EXPECT_TRUE(rb_->read_next(sub1, msg));
    EXPECT_STREQ(static_cast<const char*>(msg->get_data()), "msg2");
    EXPECT_TRUE(rb_->read_next(sub1, msg));
    EXPECT_STREQ(static_cast<const char*>(msg->get_data()), "msg3");

    // sub2从msg1开始读
    EXPECT_TRUE(rb_->read_next(sub2, msg));
    EXPECT_STREQ(static_cast<const char*>(msg->get_data()), "msg1");
}

// ==============================
// 零拷贝API
// ==============================
TEST_F(RingBufferTest, ZeroCopyReserveCommit) {
    // 预留空间
    auto token = rb_->reserve(256);
    EXPECT_TRUE(token.valid);

    // 填充数据
    auto* data = static_cast<char*>(token.msg->get_data());
    strcpy(data, "Zero-copy message");

    // 提交
    EXPECT_TRUE(rb_->commit(token, strlen("Zero-copy message") + 1, 1));

    // 验证读取
    auto* sub = rb_->register_subscriber(1, "zc_sub");
    Message* msg = nullptr;
    EXPECT_TRUE(rb_->read_next(sub, msg));
    EXPECT_STREQ(static_cast<const char*>(msg->get_data()), "Zero-copy message");
}

TEST_F(RingBufferTest, ZeroCopyAbort) {
    // 预留但不提交
    auto token = rb_->reserve(1024);
    EXPECT_TRUE(token.valid);

    // 放弃预留
    rb_->abort(token);

    // 发布正常消息
    EXPECT_TRUE(rb_->publish_message("after abort", 12));

    // 订阅者应能读到正常消息
    auto* sub = rb_->register_subscriber(1, "abort_sub");
    Message* msg = nullptr;
    EXPECT_TRUE(rb_->read_next(sub, msg));
    EXPECT_STREQ(static_cast<const char*>(msg->get_data()), "after abort");
}

// ==============================
// 环形回绕
// ==============================
TEST_F(RingBufferTest, WrapAround) {
    // 注册订阅者并读取第一条消息作为基准
    auto* sub = rb_->register_subscriber(1, "wrap_sub");

    // 发布第一条消息（标记为BEGIN）
    EXPECT_TRUE(rb_->publish_message("BEGIN", 6));
    Message* msg = nullptr;
    EXPECT_TRUE(rb_->read_next(sub, msg));
    EXPECT_STREQ(static_cast<const char*>(msg->get_data()), "BEGIN");

    // 填充缓冲区直到覆盖（使用大块数据加速回绕）
    char large_data[4096] = {0};
    memset(large_data, 'A', sizeof(large_data));

    // 写足够多的数据确保覆盖BEGIN的位置
    for (int i = 0; i < 20; ++i) {
        EXPECT_TRUE(rb_->publish_message(large_data, sizeof(large_data)));
    }

    // 发布一个特殊标记消息（应该在回绕后的位置）
    const char* wrap_msg = "WRAPPED";
    EXPECT_TRUE(rb_->publish_message(wrap_msg, strlen(wrap_msg) + 1));

    // 使用read_latest跳到最新，应该能读到WRAP_MSG
    EXPECT_TRUE(rb_->read_latest(sub, msg));
    EXPECT_STREQ(static_cast<const char*>(msg->get_data()), "WRAPPED");
}

TEST_F(RingBufferTest, WrapAroundOverwritesOldData) {
    // 关键测试：验证回绕后旧数据被覆盖，新数据可用
    char large_data[4096] = {0};
    memset(large_data, 'A', sizeof(large_data));

    // 阶段1：发布2条特殊标记消息（FIRST）
    EXPECT_TRUE(rb_->publish_message("FIRST_MESSAGE", 14));
    EXPECT_TRUE(rb_->publish_message("FIRST_MESSAGE", 14));

    // 阶段2：新订阅者应该能读到FIRST_MESSAGE（未回绕时）
    auto* sub_before = rb_->register_subscriber(1, "before_wrap");
    Message* msg = nullptr;
    EXPECT_TRUE(rb_->read_next(sub_before, msg));
    EXPECT_STREQ(static_cast<const char*>(msg->get_data()), "FIRST_MESSAGE");

    // 阶段3：发布大量消息，触发回绕（约23×4KB=92KB > 60KB，必然覆盖）
    for (int i = 0; i < 23; ++i) {
        EXPECT_TRUE(rb_->publish_message(large_data, sizeof(large_data)));
    }

    // 阶段4：再发布一条特殊标记消息（LAST），应该在回绕后的位置
    EXPECT_TRUE(rb_->publish_message("LAST_MESSAGE", 13));

    // 阶段5：使用read_latest读取最新消息，应该读到LAST_MESSAGE
    auto* sub_after = rb_->register_subscriber(2, "after_wrap");
    EXPECT_TRUE(rb_->read_latest(sub_after, msg));
    EXPECT_STREQ(static_cast<const char*>(msg->get_data()), "LAST_MESSAGE");

    // 阶段6：验证FIRST_MESSAGE已被覆盖（sub_before的read_pos指向的数据已无效）
    // 由于回绕，sub_before尝试读next可能读到A或LAST，但绝不会是FIRST
    if (rb_->read_next(sub_before, msg)) {
        std::string data(static_cast<const char*>(msg->get_data()),
                        std::min(size_t(msg->header.data_size), size_t(5)));
        EXPECT_NE(data, "FIRST") << "FIRST_MESSAGE should be overwritten after wrap-around";
    }
}

// ==============================
// read_latest 行为
// ==============================
TEST_F(RingBufferTest, ReadLatest) {
    // 发布多条消息
    rb_->publish_message("old1", 5);
    rb_->publish_message("old2", 5);
    rb_->publish_message("latest", 7);

    auto* sub = rb_->register_subscriber(1, "latest_sub");

    // read_latest应该读到最新的
    Message* msg = nullptr;
    EXPECT_TRUE(rb_->read_latest(sub, msg));
    EXPECT_STREQ(static_cast<const char*>(msg->get_data()), "latest");

    // 再次read_latest应该无新消息（已是最新）
    EXPECT_FALSE(rb_->read_next(sub, msg));
}

// ==============================
// 边界条件
// ==============================
TEST_F(RingBufferTest, EmptyMessage) {
    // 发布空消息
    EXPECT_TRUE(rb_->publish_message(nullptr, 0));

    auto* sub = rb_->register_subscriber(1, "empty_sub");
    Message* msg = nullptr;
    EXPECT_TRUE(rb_->read_next(sub, msg));
    EXPECT_EQ(msg->header.data_size, 0);
}

TEST_F(RingBufferTest, LargeMessage) {
    // 8KB消息
    std::vector<char> large_data(8192, 'X');
    large_data.back() = '\0';

    EXPECT_TRUE(rb_->publish_message(large_data.data(), large_data.size()));

    auto* sub = rb_->register_subscriber(1, "large_sub");
    Message* msg = nullptr;
    EXPECT_TRUE(rb_->read_next(sub, msg));
    EXPECT_EQ(msg->header.data_size, large_data.size());
    EXPECT_EQ(static_cast<const char*>(msg->get_data())[0], 'X');
    EXPECT_EQ(static_cast<const char*>(msg->get_data())[8191], '\0');
}

TEST_F(RingBufferTest, BufferFull) {
    // 持续写入直到触发覆盖（环形缓冲区特性）
    char chunk[2048] = {0};
    memset(chunk, 'F', sizeof(chunk));

    // 64KB缓冲区大约能存30个2KB块，写50个必然触发覆盖
    int written = 0;
    for (int i = 0; i < 50; ++i) {
        EXPECT_TRUE(rb_->publish_message(chunk, sizeof(chunk)));
        written++;
    }

    EXPECT_GE(written, 30) << "Should write multiple messages before wrap";
}

// ==============================
// 订阅者管理
// ==============================
TEST_F(RingBufferTest, SubscriberRegistration) {
    auto* sub1 = rb_->register_subscriber(1, "sub1");
    auto* sub2 = rb_->register_subscriber(2, "sub2");
    auto* sub3 = rb_->register_subscriber(3, "sub3");

    ASSERT_NE(sub1, nullptr);
    ASSERT_NE(sub2, nullptr);
    ASSERT_NE(sub3, nullptr);

    // 每个订阅者有独立ID
    EXPECT_EQ(sub1->subscriber_id, 1);
    EXPECT_EQ(sub2->subscriber_id, 2);
    EXPECT_EQ(sub3->subscriber_id, 3);
}

TEST_F(RingBufferTest, Unsubscribe) {
    auto* sub = rb_->register_subscriber(1, "temp_sub");
    ASSERT_NE(sub, nullptr);

    // 发布消息后取消订阅
    rb_->publish_message("test", 5);
    rb_->unregister_subscriber(sub);

    // 重新注册同名订阅者（新ID）
    auto* sub2 = rb_->register_subscriber(2, "new_sub");
    ASSERT_NE(sub2, nullptr);
}

// ==============================
// 多线程压力测试
// ==============================
TEST_F(RingBufferTest, ConcurrentPublish) {
    const int num_messages = 1000;
    std::atomic<int> publish_count{0};

    // 单生产者线程
    std::thread producer([&]() {
        for (int i = 0; i < num_messages; ++i) {
            std::string msg = "MSG_" + std::to_string(i);
            while (!rb_->publish_message(msg.c_str(), msg.size() + 1)) {
                // 缓冲区满时自旋等待
                std::this_thread::yield();
            }
            publish_count++;
        }
    });

    // 消费者线程
    auto* sub = rb_->register_subscriber(1, "concurrent_sub");
    std::thread consumer([&]() {
        int read_count = 0;
        while (read_count < num_messages) {
            Message* msg = nullptr;
            if (rb_->read_next(sub, msg)) {
                read_count++;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(publish_count, num_messages);
}
