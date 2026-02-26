/**
 * @file test_null_transport.cpp
 * @brief NullTransport 单元测试
 *
 * NullTransport 是一个占位实现，用于构建设备聚合与能力探测流程。
 * 所有操作都返回默认值，不访问实际硬件。
 */

#include <gtest/gtest.h>

#include "MB_DDF/PhysicalLayer/ControlPlane/NullTransport.h"

using namespace MB_DDF::PhysicalLayer::ControlPlane;
using MB_DDF::PhysicalLayer::TransportConfig;

// ==============================
// NullTransport 基础测试
// ==============================
TEST(NullTransportTest, OpenAlwaysSucceeds) {
    NullTransport transport;
    TransportConfig cfg{};

    // open 始终返回 true
    EXPECT_TRUE(transport.open(cfg));

    // 重复 open 也返回 true
    EXPECT_TRUE(transport.open(cfg));
}

TEST(NullTransportTest, CloseDoesNothing) {
    NullTransport transport;

    // close 是空操作，不应该崩溃
    transport.close();

    // 即使未 open 也能 close
    transport.close();
}

TEST(NullTransportTest, GetMappedBaseReturnsNull) {
    NullTransport transport;

    EXPECT_EQ(transport.getMappedBase(), nullptr);
}

TEST(NullTransportTest, GetMappedLengthReturnsZero) {
    NullTransport transport;

    EXPECT_EQ(transport.getMappedLength(), 0);
}

TEST(NullTransportTest, ReadReg32ReturnsFalse) {
    NullTransport transport;

    uint32_t value = 0xDEADBEEF;
    EXPECT_FALSE(transport.readReg32(0x1000, value));

    // value 应该保持不变（还是传入的值）
    EXPECT_EQ(value, 0xDEADBEEF);
}

TEST(NullTransportTest, WriteReg32ReturnsFalse) {
    NullTransport transport;

    EXPECT_FALSE(transport.writeReg32(0x1000, 0x12345678));
}

TEST(NullTransportTest, WaitEventReturnsZero) {
    NullTransport transport;

    uint32_t eventData = 0;
    int result = transport.waitEvent(&eventData, 100);

    EXPECT_EQ(result, 0);
}

TEST(NullTransportTest, GetEventFdReturnsInvalid) {
    NullTransport transport;

    EXPECT_EQ(transport.getEventFd(), -1);
}

TEST(NullTransportTest, SetCallbacksAcceptButIgnore) {
    NullTransport transport;

    bool callbackCalled = false;

    // 设置回调是安全的，但不会被调用
    transport.setOnContinuousWriteComplete([&callbackCalled](ssize_t) {
        callbackCalled = true;
    });

    transport.setOnContinuousReadComplete([&callbackCalled](ssize_t) {
        callbackCalled = true;
    });

    EXPECT_FALSE(callbackCalled);
}

TEST(NullTransportTest, ContinuousOperationsReturnFalse) {
    NullTransport transport;

    uint8_t buffer[64] = {0};

    EXPECT_FALSE(transport.continuousWrite(0, buffer, sizeof(buffer)));
    EXPECT_FALSE(transport.continuousRead(0, buffer, sizeof(buffer)));
    EXPECT_FALSE(transport.continuousWriteAt(0, buffer, sizeof(buffer), 0x1000));
    EXPECT_FALSE(transport.continuousReadAt(0, buffer, sizeof(buffer), 0x1000));
    EXPECT_FALSE(transport.continuousWriteAsync(0, buffer, sizeof(buffer), 0x1000));
    EXPECT_FALSE(transport.continuousReadAsync(0, buffer, sizeof(buffer), 0x1000));
}

TEST(NullTransportTest, XferReturnsFalse) {
    NullTransport transport;

    uint8_t tx[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint8_t rx[8] = {0};

    EXPECT_FALSE(transport.xfer(tx, rx, sizeof(tx)));

    // rx 应该保持不变（全 0）
    for (size_t i = 0; i < sizeof(rx); ++i) {
        EXPECT_EQ(rx[i], 0);
    }
}

// ==============================
// 使用场景测试
// ==============================
TEST(NullTransportTest, UsedAsPlaceholderInAggregation) {
    // 模拟设备聚合场景：NullTransport 作为占位符
    NullTransport transport;

    // 1. 尝试初始化（始终成功）
    TransportConfig cfg{};
    ASSERT_TRUE(transport.open(cfg));

    // 2. 检查能力（所有查询都返回默认值）
    EXPECT_EQ(transport.getMappedBase(), nullptr);
    EXPECT_EQ(transport.getMappedLength(), 0);
    EXPECT_EQ(transport.getEventFd(), -1);

    // 3. 尝试操作（都失败但不崩溃）
    uint32_t regValue = 0;
    EXPECT_FALSE(transport.readReg32(0, regValue));
    EXPECT_FALSE(transport.writeReg32(0, 0));

    // 4. 清理（安全）
    transport.close();
}

TEST(NullTransportTest, LifecycleTest) {
    // 完整生命周期测试
    {
        NullTransport transport;

        // 创建后状态
        EXPECT_EQ(transport.getEventFd(), -1);

        // open
        EXPECT_TRUE(transport.open({}));

        // 操作（都失败）
        uint8_t buf[16];
        EXPECT_FALSE(transport.continuousRead(0, buf, 16));

        // close
        transport.close();
    }  // 析构

    // 如果走到这里说明没有崩溃
    SUCCEED();
}
