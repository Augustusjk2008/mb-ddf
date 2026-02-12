/**
 * @file test_message.cpp
 * @brief Message和CRC32单元测试
 */

#include <gtest/gtest.h>
#include "MB_DDF/DDS/Message.h"

using namespace MB_DDF::DDS;

// ==============================
// CRC32算法验证
// ==============================
TEST(MessageCRC32, AlgorithmVerification) {
    // 验证文档中的标准CRC32测试向量
    // 数据: AA BB CC DD EE FF 00 11
    // 期望: 0x891A7844 (反向算法)
    EXPECT_EQ(Message::verify_crc32_algorithms(), 0);
}

TEST(MessageCRC32, KnownValue) {
    const uint8_t data[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
    constexpr size_t test_size = sizeof(data);
    constexpr uint32_t expected_crc = 0x891A7844;

    uint32_t crc = MessageHeader::calculate_crc32_reflected(data, test_size);
    EXPECT_EQ(crc, expected_crc);
}

TEST(MessageCRC32, EmptyData) {
    uint32_t crc = MessageHeader::calculate_crc32_reflected(nullptr, 0);
    EXPECT_EQ(crc, 0);

    crc = MessageHeader::calculate_crc32_reflected(reinterpret_cast<const uint8_t*>(""), 0);
    EXPECT_EQ(crc, 0);
}

// ==============================
// Message构造与验证
// ==============================
TEST(MessageConstruct, BasicMessage) {
    const char* data = "Hello, DDS!";
    size_t data_size = strlen(data) + 1;

    Message msg(1, 100, const_cast<void*>(static_cast<const void*>(data)), data_size);

    EXPECT_EQ(msg.header.topic_id, 1);
    EXPECT_EQ(msg.header.sequence, 100);
    EXPECT_EQ(msg.header.data_size, data_size);
    EXPECT_TRUE(msg.header.is_valid());
}

TEST(MessageConstruct, ChecksumVerification) {
    const char* data = "Test message for checksum";
    size_t data_size = strlen(data) + 1;

    // 正确用法：分配足够内存容纳 Message + 数据
    size_t total_size = sizeof(Message) + data_size;
    auto* storage = new uint8_t[total_size];

    // placement new 构造 Message（不传入数据指针，避免构造函数计算错误校验和）
    auto* msg = new (storage) Message();
    msg->header.topic_id = 42;
    msg->header.sequence = 999;
    msg->header.data_size = data_size;
    msg->header.set_timestamp();

    // 复制数据到 Message 后面的正确位置
    memcpy(msg->get_data(), data, data_size);

    // 现在计算校验和（基于实际存储位置的数据）
    msg->header.set_checksum(msg->get_data(), data_size);

    // 验证消息有效性（包含校验和）
    EXPECT_TRUE(msg->is_valid(true));

    delete[] storage;
}

TEST(MessageConstruct, ZeroData) {
    Message msg(1, 1, nullptr, 0);

    EXPECT_EQ(msg.header.data_size, 0);
    EXPECT_TRUE(msg.is_valid(true));  // 空数据消息也是有效的
}

// ==============================
// 数据指针访问
// ==============================
TEST(MessageData, DataAccess) {
    char buffer[] = "Test data buffer";
    size_t data_size = strlen(buffer) + 1;

    // 在堆上分配消息+数据空间模拟共享内存布局
    size_t total_size = sizeof(Message) + data_size;
    auto* storage = new uint8_t[total_size];

    // placement new构造Message
    auto* msg = new (storage) Message(1, 1, nullptr, 0);
    msg->header.data_size = data_size;

    // 复制数据到消息后面
    memcpy(msg->get_data(), buffer, data_size);

    // 验证数据可读
    EXPECT_STREQ(static_cast<const char*>(msg->get_data()), buffer);

    delete[] storage;
}

// ==============================
// 时间戳测试
// ==============================
TEST(MessageTimestamp, TimestampSet) {
    Message msg;

    auto before = std::chrono::steady_clock::now();
    msg.header.set_timestamp();
    auto after = std::chrono::steady_clock::now();

    // 验证时间戳在合理范围内（纳秒级）
    auto msg_time = std::chrono::nanoseconds(msg.header.timestamp);
    auto msg_tp = std::chrono::steady_clock::time_point(msg_time);

    EXPECT_GE(msg_tp, before);
    EXPECT_LE(msg_tp, after);
}

// ==============================
// 消息大小计算
// ==============================
TEST(MessageSize, TotalSize) {
    EXPECT_EQ(Message::total_size(0), sizeof(MessageHeader));
    EXPECT_EQ(Message::total_size(100), sizeof(MessageHeader) + 100);
    EXPECT_EQ(Message::total_size(1024), sizeof(MessageHeader) + 1024);
}

TEST(MessageSize, MsgSizeMethod) {
    Message msg;
    msg.header.data_size = 256;

    EXPECT_EQ(msg.msg_size(), sizeof(MessageHeader) + 256);
}

// ==============================
// 边界条件
// ==============================
TEST(MessageBoundary, LargeDataSize) {
    // 测试大数据（接近32位上限）
    Message msg(0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF, nullptr, 0xFFFFFFFF);

    EXPECT_EQ(msg.header.topic_id, 0xFFFFFFFF);
    EXPECT_EQ(msg.header.sequence, 0xFFFFFFFFFFFFFFFF);
    EXPECT_EQ(msg.header.data_size, 0xFFFFFFFF);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
