/**
 * @file DatalinkDevice.cpp
 */
#include "MB_DDF/PhysicalLayer/Device/DatalinkDevice.h"
#include "MB_DDF/PhysicalLayer/Support/Log.h"
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <errno.h>

namespace MB_DDF {
namespace PhysicalLayer {
namespace Device {

namespace {
// 寄存器地址定义
static constexpr uint64_t CTRL_REG_0 = 0x0;    // 控制寄存器0（8位）
static constexpr uint64_t CTRL_REG_1 = 0x1;    // 控制寄存器1（8位）
static constexpr uint64_t OFFSET_REG = 0x2;    // 偏移寄存器（16位）

// 数据缓冲区起始地址
static constexpr uint64_t WRITE_BUF_START = 0x33 * 4;  // 0xCC，写数据起始地址
static constexpr uint64_t READ_BUF_START_0 = 0x1 * 4;      // offset=0时读数据起始地址
static constexpr uint64_t READ_BUF_START_1 = 0x1A * 4;     // offset=1时读数据起始地址

static constexpr uint32_t MAX_DATA_LEN = 100;  // 最大数据字节数
}

bool DatalinkDevice::open(const LinkConfig& cfg) {
    if (!TransportLinkAdapter::open(cfg)) {
        LOGE("datalink", "open", -1, "adapter base open failed");
        return false;
    }

    auto& tp = transport();
    if (tp.getMappedBase() == nullptr || tp.getMappedLength() == 0) {
        LOGE("datalink", "open", -1, "register space unmapped");
        return false;
    }

    LOGI("datalink", "open", 0, "mtu=%u, regs=mmapped", getMTU());
    return true;
}

bool DatalinkDevice::close() {
    return TransportLinkAdapter::close();
}

bool DatalinkDevice::send(const uint8_t* data, uint32_t len) {
    auto& tp = transport();
    if (!data || len == 0) return true;
    if (tp.getMappedBase() == nullptr) return false;

    // 限制最大写入长度
    uint32_t write_len = (len > MAX_DATA_LEN) ? MAX_DATA_LEN : len;

    // Step 1: 0x0地址写8位0x1（开始写操作）
    if (!wr8(CTRL_REG_0, 0x1)) {
        LOGE("datalink", "send", -1, "failed to write 0x1 to 0x0");
        return false;
    }

    // Step 2: 从0x33*4(0xCC)开始写数据，每次写32位，+0x4地址，最多写100字节
    uint32_t bram_offset = 0;
    while (bram_offset < write_len) {
        uint32_t w = 0;
        // 组装4字节（不足补零）
        for (int i = 0; i < 4; ++i) {
            uint32_t idx = bram_offset + i;
            uint8_t b = (idx < write_len) ? data[idx] : 0;
            w |= (static_cast<uint32_t>(b) << (8 * i));
        }
        if (!wr32(WRITE_BUF_START + bram_offset, w)) {
            LOGE("datalink", "send", -1, "failed to write 32-bit data at offset 0x%x", WRITE_BUF_START + bram_offset);
            return false;
        }
        bram_offset += 4;
    }

    // Step 3: 0x0地址写8位0x0（结束写操作）
    if (!wr8(CTRL_REG_0, 0x0)) {
        LOGE("datalink", "send", -1, "failed to write 0x0 to 0x0");
        return false;
    }

    return true;
}

int32_t DatalinkDevice::receive(uint8_t* buf, uint32_t buf_size) {
    auto& tp = transport();
    if (!buf || buf_size == 0) return 0;
    if (tp.getMappedBase() == nullptr) return -1;

    // Step 1: 0x1地址写8位0x1（开始读操作）
    if (!wr8(CTRL_REG_1, 0x1)) {
        LOGE("datalink", "receive", -1, "failed to write 0x1 to 0x1");
        return -1;
    }

    // Step 2: 从0x2地址读16位offset
    uint16_t offset = 0;
    if (!rd16(OFFSET_REG, offset)) {
        LOGE("datalink", "receive", -1, "failed to read offset from 0x2");
        return -1;
    }

    // Step 3: 根据offset值确定读数据的起始地址
    uint64_t read_start_addr;
    if (offset == 0) {
        read_start_addr = READ_BUF_START_0;  // 从0x1 * 4开始读
    } else if (offset == 1) {
        read_start_addr = READ_BUF_START_1;  // 从0x1A * 4开始读
    } else {
        // 无效的offset值，写0x0到0x1并返回错误
        wr8(CTRL_REG_1, 0x0);
        LOGE("datalink", "receive", -1, "invalid offset value: %u", offset);
        return -1;
    }

    // Step 4: 从确定的地址开始读数据，每次读32位，+0x4地址，最多读100字节
    uint32_t read_len = (buf_size > MAX_DATA_LEN) ? MAX_DATA_LEN : buf_size;
    uint32_t bram_offset = 0;
    uint32_t produced = 0;

    while (produced < read_len) {
        uint32_t w = 0;
        if (!rd32(read_start_addr + bram_offset, w)) {
            break;
        }
        uint8_t bytes[4];
        std::memcpy(bytes, &w, sizeof(w));
        for (int i = 0; i < 4 && produced < read_len; ++i) {
            buf[produced] = bytes[i];
            ++produced;
        }
        bram_offset += 4;
    }

    // Step 5: 0x1地址写8位0x0（结束读操作）
    if (!wr8(CTRL_REG_1, 0x0)) {
        LOGE("datalink", "receive", -1, "failed to write 0x0 to 0x1");
        return -1;
    }

    return static_cast<int32_t>(produced);
}

int32_t DatalinkDevice::receive(uint8_t* buf, uint32_t buf_size, uint32_t timeout_us) {
    // 简单实现：先尝试非阻塞读取，如果没有数据则轮询等待
    auto& tp = transport();

    // 检查是否有event fd支持
    if (tp.getEventFd() >= 0) {
        uint32_t bitmap = 0;
        int ev = tp.waitEvent(&bitmap, timeout_us / 1000);
        if (ev <= 0) {
            return ev == 0 ? 0 : -1;
        }
        return receive(buf, buf_size);
    }

    // 轮询模式
    const uint32_t step_us = 100;
    uint32_t waited = 0;
    int32_t result;

    while (waited <= timeout_us) {
        result = receive(buf, buf_size);
        if (result != 0) {
            return result;
        }
        usleep(step_us);
        waited += step_us;
    }

    return 0; // 超时
}

int DatalinkDevice::ioctl(uint32_t opcode, const void* in, size_t in_len, void* out, size_t out_len) {
    // 不支持ioctl操作
    (void)opcode;
    (void)in;
    (void)in_len;
    (void)out;
    (void)out_len;
    return -ENOSYS;
}

} // namespace Device
} // namespace PhysicalLayer
} // namespace MB_DDF
