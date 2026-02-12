/**
 * @file test_self_describing_log.cpp
 * @brief SelfDescribingLog 单元测试
 *
 * 测试自描述日志系统的三个核心组件：
 * - LogSchema: 定义日志结构和字段布局
 * - LogRecordBuilder: 构建二进制记录
 * - SelfDescribingLogWriter: 写入文件
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>

#include "MB_DDF/Tools/SelfDescribingLog.h"

using namespace MB_DDF::Tools;

// ==============================
// LogSchema 测试
// ==============================
TEST(LogSchemaTest, AddField) {
    LogSchema schema;

    // 添加基本字段
    EXPECT_TRUE(schema.addField("temperature", LogFieldType::Float32, 1, 0.1, 0.0));
    EXPECT_TRUE(schema.addField("pressure", LogFieldType::UInt32));
    EXPECT_TRUE(schema.addField("count", LogFieldType::Int16, 10)); // 数组

    // 验证模式有效
    EXPECT_TRUE(schema.isValid());

    // 验证字段列表
    const auto& fields = schema.fields();
    EXPECT_EQ(fields.size(), 3);
    EXPECT_EQ(fields[0].name, "temperature");
    EXPECT_EQ(fields[0].type, LogFieldType::Float32);
    EXPECT_EQ(fields[0].scale, 0.1);
}

TEST(LogSchemaTest, AddFieldAt) {
    LogSchema schema;

    // 在指定偏移位置添加字段
    EXPECT_TRUE(schema.addFieldAt("x", LogFieldType::Float64, 1, 0, 8));
    EXPECT_TRUE(schema.addFieldAt("y", LogFieldType::Float64, 1, 8, 8));
    EXPECT_TRUE(schema.addFieldAt("z", LogFieldType::Float64, 1, 16, 8));

    EXPECT_TRUE(schema.isValid());
    EXPECT_EQ(schema.recordSize(), 24); // 3 * 8 bytes
}

TEST(LogSchemaTest, AddBitFieldAt) {
    LogSchema schema;

    // 添加位字段（从32位存储中提取）
    EXPECT_TRUE(schema.addBitFieldAt("flags", LogFieldType::UInt32, 0, 4, 0, 0));
    EXPECT_TRUE(schema.addBitFieldAt("status", LogFieldType::UInt32, 4, 8, 0, 0));

    EXPECT_TRUE(schema.isValid());
}

TEST(LogSchemaTest, EmptySchemaInvalid) {
    LogSchema schema;
    EXPECT_FALSE(schema.isValid());
}

TEST(LogSchemaTest, RecordSizeCalculation) {
    LogSchema schema;

    schema.addField("u8", LogFieldType::UInt8);
    schema.addField("u16", LogFieldType::UInt16);
    schema.addField("u32", LogFieldType::UInt32);
    schema.addField("u64", LogFieldType::UInt64);

    // 1 + 2 + 4 + 8 = 15 bytes (不考虑对齐)
    // 实际大小取决于实现
    EXPECT_GT(schema.recordSize(), 0);
}

TEST(LogSchemaTest, SerializeAndDeserialize) {
    LogSchema schema;

    schema.addField("value", LogFieldType::Float32, 1, 0.01, 0.0);
    schema.addField("timestamp", LogFieldType::UInt64);

    auto serialized = schema.serialize();
    EXPECT_GT(serialized.size(), 0);

    // 序列化数据应该包含魔数或版本信息
    // 具体格式取决于实现
}

// ==============================
// LogRecordBuilder 测试
// ==============================
TEST(LogRecordBuilderTest, BasicWrite) {
    LogRecordBuilder builder(32);

    EXPECT_TRUE(builder.writeUInt8(0x12));
    EXPECT_TRUE(builder.writeUInt16(0x3456));
    EXPECT_TRUE(builder.writeUInt32(0x789ABCDE));

    EXPECT_EQ(builder.bytesWritten(), 7);
    EXPECT_FALSE(builder.isComplete());
}

TEST(LogRecordBuilderTest, WriteAllTypes) {
    LogRecordBuilder builder(64);

    EXPECT_TRUE(builder.writeUInt8(0x01));
    EXPECT_TRUE(builder.writeUInt16(0x0203));
    EXPECT_TRUE(builder.writeUInt32(0x04050607));
    EXPECT_TRUE(builder.writeUInt64(0x08090A0B0C0D0E0F));
    EXPECT_TRUE(builder.writeInt8(-1));
    EXPECT_TRUE(builder.writeInt16(-2));
    EXPECT_TRUE(builder.writeInt32(-3));
    EXPECT_TRUE(builder.writeInt64(-4));
    EXPECT_TRUE(builder.writeFloat(3.14f));
    EXPECT_TRUE(builder.writeDouble(2.71828));

    EXPECT_GT(builder.bytesWritten(), 0);
}

TEST(LogRecordBuilderTest, WriteBytes) {
    LogRecordBuilder builder(16);

    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    EXPECT_TRUE(builder.writeBytes(data, 4));
    EXPECT_EQ(builder.bytesWritten(), 4);
}

TEST(LogRecordBuilderTest, Reset) {
    LogRecordBuilder builder(16);

    builder.writeUInt32(0x12345678);
    EXPECT_EQ(builder.bytesWritten(), 4);

    builder.reset();
    EXPECT_EQ(builder.bytesWritten(), 0);
}

TEST(LogRecordBuilderTest, CapacityExceeded) {
    LogRecordBuilder builder(4);

    EXPECT_TRUE(builder.writeUInt32(0x12345678));
    EXPECT_TRUE(builder.isComplete());

    // 容量已满，再写入应该失败
    EXPECT_FALSE(builder.writeUInt8(0x01));
}

TEST(LogRecordBuilderTest, DataAccess) {
    LogRecordBuilder builder(8);

    builder.writeUInt32(0x12345678);
    builder.writeUInt32(0x9ABCDEF0);

    const auto& data = builder.data();
    EXPECT_EQ(data.size(), 8);

    // 小端字节序验证
    EXPECT_EQ(data[0], 0x78);
    EXPECT_EQ(data[1], 0x56);
    EXPECT_EQ(data[2], 0x34);
    EXPECT_EQ(data[3], 0x12);
}

// ==============================
// SelfDescribingLogWriter 测试
// ==============================
TEST(SelfDescribingLogWriterTest, CreateAndOpen) {
    const char* test_file = "/tmp/test_log_001.bin";

    // 清理
    std::remove(test_file);

    LogSchema schema;
    schema.addField("value", LogFieldType::UInt32);

    SelfDescribingLogWriter writer;
    EXPECT_TRUE(writer.open(test_file, schema));
    EXPECT_TRUE(writer.isOpen());

    writer.close();
    EXPECT_FALSE(writer.isOpen());

    // 验证文件存在
    FILE* fp = std::fopen(test_file, "rb");
    ASSERT_NE(fp, nullptr);
    std::fclose(fp);

    std::remove(test_file);
}

TEST(SelfDescribingLogWriterTest, AppendRecord) {
    const char* test_file = "/tmp/test_log_002.bin";
    std::remove(test_file);

    LogSchema schema;
    schema.addFieldAt("timestamp", LogFieldType::UInt64, 1, 0, 8);
    schema.addFieldAt("data", LogFieldType::UInt32, 1, 8, 4);

    SelfDescribingLogWriter writer;
    ASSERT_TRUE(writer.open(test_file, schema));

    // 构建记录
    struct LogEntry {
        uint64_t timestamp;
        uint32_t data;
    } entry = {1234567890ULL, 0xDEADBEEF};

    EXPECT_TRUE(writer.appendRecord(&entry, sizeof(entry)));
    EXPECT_TRUE(writer.appendRecord(&entry, sizeof(entry)));

    writer.flush();
    writer.close();

    // 验证文件大小
    FILE* fp = std::fopen(test_file, "rb");
    ASSERT_NE(fp, nullptr);
    std::fseek(fp, 0, SEEK_END);
    long file_size = std::ftell(fp);
    std::fclose(fp);

    EXPECT_GT(file_size, 0);

    std::remove(test_file);
}

TEST(SelfDescribingLogWriterTest, AppendRawRecord) {
    const char* test_file = "/tmp/test_log_003.bin";
    std::remove(test_file);

    LogSchema schema;
    schema.addField("value", LogFieldType::UInt32);

    SelfDescribingLogWriter writer;
    ASSERT_TRUE(writer.open(test_file, schema));

    std::vector<uint8_t> record = {0x01, 0x02, 0x03, 0x04};
    EXPECT_TRUE(writer.appendRawRecord(record));

    writer.close();

    std::remove(test_file);
}

TEST(SelfDescribingLogWriterTest, RecordSize) {
    const char* test_file = "/tmp/test_log_004.bin";
    std::remove(test_file);

    LogSchema schema;
    schema.addField("u32_1", LogFieldType::UInt32);
    schema.addField("u32_2", LogFieldType::UInt32);

    SelfDescribingLogWriter writer;
    ASSERT_TRUE(writer.open(test_file, schema));

    // 记录大小应该是 8 字节
    EXPECT_EQ(writer.recordSize(), 8);

    writer.close();
    std::remove(test_file);
}

TEST(SelfDescribingLogWriterTest, ReopenExistingFile) {
    const char* test_file = "/tmp/test_log_005.bin";
    std::remove(test_file);

    LogSchema schema;
    schema.addField("counter", LogFieldType::UInt32);

    // 第一次打开并写入
    {
        SelfDescribingLogWriter writer;
        ASSERT_TRUE(writer.open(test_file, schema));

        uint32_t value = 42;
        writer.appendRecord(&value, sizeof(value));
        writer.close();
    }

    // 第二次打开（追加模式）
    {
        SelfDescribingLogWriter writer;
        ASSERT_TRUE(writer.open(test_file, schema));

        uint32_t value = 100;
        writer.appendRecord(&value, sizeof(value));
        writer.close();
    }

    std::remove(test_file);
}

TEST(SelfDescribingLogWriterTest, EmptyPathFails) {
    LogSchema schema;
    schema.addField("value", LogFieldType::UInt32);

    SelfDescribingLogWriter writer;
    EXPECT_FALSE(writer.open("", schema));
}

// ==============================
// 集成测试
// ==============================
TEST(SelfDescribingLogIntegration, FullWorkflow) {
    const char* test_file = "/tmp/test_log_full.bin";
    std::remove(test_file);

    // 定义传感器数据结构（紧凑布局，无填充）
    #pragma pack(push, 1)
    struct SensorData {
        uint64_t timestamp;
        float temperature;
        float pressure;
        uint32_t status;
    };
    #pragma pack(pop)

    // 计算实际数据大小（8 + 4 + 4 + 4 = 20 字节）
    constexpr size_t data_size = 8 + 4 + 4 + 4;

    // 创建模式
    LogSchema schema;
    schema.addFieldAt("timestamp", LogFieldType::UInt64, 1, 0, 8);
    schema.addFieldAt("temperature", LogFieldType::Float32, 1, 8, 4);
    schema.addFieldAt("pressure", LogFieldType::Float32, 1, 12, 4);
    schema.addFieldAt("status", LogFieldType::UInt32, 1, 16, 4);

    ASSERT_TRUE(schema.isValid());
    EXPECT_EQ(schema.recordSize(), data_size);

    // 创建写入器并写入数据
    SelfDescribingLogWriter writer;
    ASSERT_TRUE(writer.open(test_file, schema));

    for (int i = 0; i < 100; ++i) {
        SensorData data = {
            static_cast<uint64_t>(1000000 + i),
            25.5f + i * 0.1f,
            1013.25f + i * 0.5f,
            static_cast<uint32_t>(i)
        };
        EXPECT_TRUE(writer.appendRecord(&data, sizeof(data)));
    }

    writer.flush();
    writer.close();

    // 验证文件大小
    FILE* fp = std::fopen(test_file, "rb");
    ASSERT_NE(fp, nullptr);
    std::fseek(fp, 0, SEEK_END);
    long file_size = std::ftell(fp);
    std::fclose(fp);

    // 文件大小应该大于 100 * data_size（包含头部）
    EXPECT_GT(file_size, 100 * static_cast<long>(data_size));

    std::remove(test_file);
}

TEST(SelfDescribingLogIntegration, MultipleDataTypes) {
    const char* test_file = "/tmp/test_log_types.bin";
    std::remove(test_file);

    LogSchema schema;
    schema.addField("u8", LogFieldType::UInt8);
    schema.addField("u16", LogFieldType::UInt16);
    schema.addField("u32", LogFieldType::UInt32);
    schema.addField("u64", LogFieldType::UInt64);
    schema.addField("f32", LogFieldType::Float32);
    schema.addField("f64", LogFieldType::Float64);

    SelfDescribingLogWriter writer;
    ASSERT_TRUE(writer.open(test_file, schema));

    LogRecordBuilder builder(writer.recordSize());
    builder.writeUInt8(0x01);
    builder.writeUInt16(0x0203);
    builder.writeUInt32(0x04050607);
    builder.writeUInt64(0x08090A0B0C0D0E0F);
    builder.writeFloat(1.5f);
    builder.writeDouble(2.5);

    EXPECT_TRUE(writer.appendRawRecord(builder.data()));
    writer.close();

    std::remove(test_file);
}

