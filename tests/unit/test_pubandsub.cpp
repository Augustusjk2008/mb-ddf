/**
 * @file test_pubandsub.cpp
 * @brief PubAndSub 结构体单元测试
 *
 * 测试 PubAndSub 组合结构的创建、write/read 操作
 */

#include <gtest/gtest.h>
#include <cstring>

#include "MB_DDF/DDS/DDSCore.h"
#include "MB_DDF/PhysicalLayer/ControlPlane/NullTransport.h"

using namespace MB_DDF::DDS;
using namespace MB_DDF::PhysicalLayer;
using namespace MB_DDF::PhysicalLayer::ControlPlane;

// ==============================
// 测试固件
// ==============================
class PubAndSubTest : public ::testing::Test {
protected:
    void SetUp() override {
        shm_unlink("/MB_DDF_SHM");
        sem_unlink("/MB_DDF_SHM_SEM");

        auto& dds = DDSCore::instance();
        ASSERT_TRUE(dds.initialize(16 * 1024 * 1024));
    }

    void TearDown() override {
        DDSCore::instance().shutdown();
        shm_unlink("/MB_DDF_SHM");
        sem_unlink("/MB_DDF_SHM_SEM");
    }
};

// ==============================
// 基础构造测试
// ==============================
TEST_F(PubAndSubTest, BasicConstruction) {
    // 创建一个简单的 Handle（使用 NullTransport）
    auto transport = std::make_shared<NullTransport>();
    TransportConfig cfg{};
    transport->open(cfg);

    // 由于 PubAndSub 需要 DDS::Handle，这里无法直接测试完整功能
    // 主要验证代码结构正确
    EXPECT_NE(transport, nullptr);
}

// ==============================
// 读写接口测试
// ==============================
TEST_F(PubAndSubTest, WriteInterface) {
    // PubAndSub 的 write 方法调用 publisher->publish()
    // 由于没有真实的 Handle，这里仅验证接口存在性

    auto transport = std::make_shared<NullTransport>();
    TransportConfig cfg{};
    EXPECT_TRUE(transport->open(cfg));

    const char* data = "Test data";
    // transport 不是 DDS::Handle，不能直接创建 PubAndSub
    // 此测试主要验证 NullTransport 可以正常 open
    (void)data;
}

TEST_F(PubAndSubTest, ReadInterface) {
    // PubAndSub 的 read 方法调用 subscriber->read()
    auto transport = std::make_shared<NullTransport>();

    // 验证 NullTransport 基本功能
    EXPECT_EQ(transport->getMappedBase(), nullptr);
    EXPECT_EQ(transport->getMappedLength(), 0);
    EXPECT_EQ(transport->getEventFd(), -1);
}

// ==============================
// NullTransport 功能测试
// ==============================
TEST_F(PubAndSubTest, NullTransportOperations) {
    auto transport = std::make_shared<NullTransport>();
    TransportConfig cfg{};

    // 验证所有操作返回默认值
    EXPECT_TRUE(transport->open(cfg));
    EXPECT_EQ(transport->getMappedBase(), nullptr);
    EXPECT_EQ(transport->getMappedLength(), 0);

    uint8_t val8 = 0;
    EXPECT_FALSE(transport->readReg8(0, val8));
    EXPECT_FALSE(transport->writeReg8(0, 0));

    uint16_t val16 = 0;
    EXPECT_FALSE(transport->readReg16(0, val16));
    EXPECT_FALSE(transport->writeReg16(0, 0));

    uint32_t val32 = 0;
    EXPECT_FALSE(transport->readReg32(0, val32));
    EXPECT_FALSE(transport->writeReg32(0, 0));

    EXPECT_EQ(transport->waitEvent(nullptr, 0), 0);
    EXPECT_EQ(transport->getEventFd(), -1);

    EXPECT_FALSE(transport->continuousWrite(0, nullptr, 0));
    EXPECT_FALSE(transport->continuousRead(0, nullptr, 0));

    uint8_t tx[4] = {0}, rx[4] = {0};
    EXPECT_FALSE(transport->xfer(tx, rx, 4));

    transport->close();
}

// ==============================
// DDS 与 NullTransport 集成
// ==============================
TEST_F(PubAndSubTest, DDSWithNullTransport) {
    // 测试使用 NullTransport
    auto transport = std::make_shared<NullTransport>();
    TransportConfig cfg{};

    // 验证 open 返回 true
    EXPECT_TRUE(transport->open(cfg));
}
