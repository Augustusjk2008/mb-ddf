/**
 * @file test_md5.cpp
 * @brief MD5工具测试
 */

#include <gtest/gtest.h>
#include "MB_DDF/Tools/md5.h"

using namespace MB_DDF::Tools;

// 辅助函数：将16字节结果转为十六进制字符串
static std::string to_hex(const unsigned char* data, size_t len) {
    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        result += hex[data[i] >> 4];
        result += hex[data[i] & 0x0f];
    }
    return result;
}

// ==============================
// 标准测试向量 (RFC 1321)
// ==============================
TEST(MD5, EmptyString) {
    MD5 md5;
    md5.update("");
    unsigned char result[16];
    md5.finalize(result);

    // d41d8cd98f00b204e9800998ecf8427e
    EXPECT_EQ(to_hex(result, 16), "d41d8cd98f00b204e9800998ecf8427e");
}

TEST(MD5, SingleChar) {
    MD5 md5;
    md5.update("a");
    unsigned char result[16];
    md5.finalize(result);

    // 0cc175b9c0f1b6a831c399e269772661
    EXPECT_EQ(to_hex(result, 16), "0cc175b9c0f1b6a831c399e269772661");
}

TEST(MD5, ShortString) {
    MD5 md5;
    md5.update("abc");
    unsigned char result[16];
    md5.finalize(result);

    // 900150983cd24fb0d6963f7d28e17f72
    EXPECT_EQ(to_hex(result, 16), "900150983cd24fb0d6963f7d28e17f72");
}

TEST(MD5, LongerString) {
    MD5 md5;
    md5.update("message digest");
    unsigned char result[16];
    md5.finalize(result);

    // f96b697d7cb7938d525a2f31aaf161d0
    EXPECT_EQ(to_hex(result, 16), "f96b697d7cb7938d525a2f31aaf161d0");
}

TEST(MD5, Alphabet) {
    MD5 md5;
    md5.update("abcdefghijklmnopqrstuvwxyz");
    unsigned char result[16];
    md5.finalize(result);

    // c3fcd3d76192e4007dfb496cca67e13b
    EXPECT_EQ(to_hex(result, 16), "c3fcd3d76192e4007dfb496cca67e13b");
}

TEST(MD5, Alphanumeric) {
    MD5 md5;
    md5.update("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    unsigned char result[16];
    md5.finalize(result);

    // d174ab98d277d9f5a5611c2c9f419d9f
    EXPECT_EQ(to_hex(result, 16), "d174ab98d277d9f5a5611c2c9f419d9f");
}

// ==============================
// 多次update（增量计算）
// ==============================
TEST(MD5, MultiUpdate) {
    MD5 md5;
    md5.update("a");
    md5.update("b");
    md5.update("c");
    unsigned char result[16];
    md5.finalize(result);

    // 应该和一次update("abc")相同
    EXPECT_EQ(to_hex(result, 16), "900150983cd24fb0d6963f7d28e17f72");
}

TEST(MD5, ChunkedUpdate) {
    MD5 md5;
    md5.update("The quick brown ");
    md5.update("fox jumps over ");
    md5.update("the lazy dog");
    unsigned char result[16];
    md5.finalize(result);

    // 9e107d9d372bb6826bd81d3542a419d6
    EXPECT_EQ(to_hex(result, 16), "9e107d9d372bb6826bd81d3542a419d6");
}

// ==============================
// hash便捷方法
// ==============================
TEST(MD5, HashMethod) {
    std::string result = MD5().hash("abc");
    EXPECT_EQ(result, "900150983cd24fb0d6963f7d28e17f72");
}

TEST(MD5, HashMethodLong) {
    std::string result = MD5().hash("message digest");
    EXPECT_EQ(result, "f96b697d7cb7938d525a2f31aaf161d0");
}

// ==============================
// 大输入测试
// ==============================
TEST(MD5, LargeInput) {
    std::string input(10000, 'X');

    MD5 md5;
    md5.update(input);
    unsigned char result[16];
    md5.finalize(result);

    // 不为空且长度为32
    std::string hex_result = to_hex(result, 16);
    EXPECT_EQ(hex_result.length(), 32);
    EXPECT_NE(hex_result, "d41d8cd98f00b204e9800998ecf8427e");  // 不是空串结果
}

// ==============================
// 二进制数据
// ==============================
TEST(MD5, BinaryData) {
    unsigned char data[] = {0x00, 0x01, 0x02, 0x03, 0xFF, 0xFE, 0xFD, 0xFC};

    MD5 md5;
    md5.update(data, sizeof(data));
    unsigned char result[16];
    md5.finalize(result);

    std::string hex_result = to_hex(result, 16);
    EXPECT_EQ(hex_result.length(), 32);
}

// ==============================
// reset重置
// ==============================
TEST(MD5, Reset) {
    MD5 md5;
    md5.update("abc");
    md5.reset();
    md5.update("");
    unsigned char result[16];
    md5.finalize(result);

    // 重置后计算空串，应该得到空串的MD5
    EXPECT_EQ(to_hex(result, 16), "d41d8cd98f00b204e9800998ecf8427e");
}

// ==============================
// 静态hash方法
// ==============================
TEST(MD5, StaticHash) {
    unsigned char result[16];
    unsigned char input[] = "abc";
    MD5::hash(input, 3, result);

    EXPECT_EQ(to_hex(result, 16), "900150983cd24fb0d6963f7d28e17f72");
}
