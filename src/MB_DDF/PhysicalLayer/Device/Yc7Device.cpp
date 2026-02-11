/**
 * @file Yc7Device.cpp
 */
#include "MB_DDF/PhysicalLayer/Device/Yc7Device.h"
#include "MB_DDF/PhysicalLayer/Support/Log.h"
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <errno.h>

namespace MB_DDF {
namespace PhysicalLayer {
namespace Device {

namespace {
// RS422 寄存器/缓冲区偏移
static constexpr uint64_t RECV_BUF = 0x0000; // BRAM 接收缓冲区起始
static constexpr uint64_t SEND_BUF = 0x1000; // BRAM 发送缓冲区起始
static constexpr uint64_t UCR_reg  = 0x2000; // UART 控制寄存器（8位）
static constexpr uint64_t MCR_reg  = 0x2001; // 模式控制寄存器（8位）
static constexpr uint64_t BRSR_reg = 0x2002; // 波特率选择寄存器（8位）
static constexpr uint64_t ICR_reg  = 0x2003; // 中断/状态控制寄存器（8位）
static constexpr uint64_t THL_reg  = 0x2004; // 发送头低字节（8位）
static constexpr uint64_t THH_reg  = 0x2005; // 发送头高字节（8位）
static constexpr uint64_t RHL_reg  = 0x2006; // 接收头低字节（8位）
static constexpr uint64_t RHH_reg  = 0x2007; // 接收头高字节（8位）

// static constexpr uint64_t TTL_reg  = 0x208; // 发送尾低字节（8位，可选）
// static constexpr uint64_t TTH_reg  = 0x209; // 发送尾高字节（8位，可选）
// static constexpr uint64_t RTL_reg  = 0x20A; // 接收尾低字节（8位，可选）
// static constexpr uint64_t RTH_reg  = 0x20B; // 接收尾高字节（8位，可选）
static constexpr uint64_t LPB_reg  = 0x200C; // 回环模式寄存器（8位）
static constexpr uint64_t INT_reg  = 0x200D; // 中断模式寄存器（8位）
static constexpr uint64_t EVT_reg  = 0x2010; // 事件宽度寄存器（16位）

static constexpr uint64_t YCL_reg  = 0x2014; // 遥测协议使能寄存器（低8位）
static constexpr uint64_t YCH_reg  = 0x2015; // 遥测协议使能寄存器（高8位）
static constexpr uint64_t LNL_reg  = 0x2018; // 遥测协议长度寄存器（低8位）
static constexpr uint64_t LNH_reg  = 0x2019; // 遥测协议长度寄存器（高8位） 

static constexpr uint64_t CMD_reg  = 0x3000; // 命令/状态寄存器（写入命令用 32 位，读取状态用 8 位）
static constexpr uint64_t STU_reg  = 0x3000; // 状态寄存器（与 CMD 同址，读 8 位）
static constexpr uint64_t ERR_reg  = 0x3004; // 错误寄存器（8位）

// 状态位
static constexpr uint8_t STU_RX_READY_MASK = 0x01; // bit0：接收有数据
static constexpr uint8_t STU_TX_READY_MASK = 0x02; // bit1：发送可用

// 命令值
static constexpr uint32_t CMD_TX = 0x81; // 触发发送
static constexpr uint32_t CMD_RX = 0x82; // 触发接收
}

bool Yc7Device::open(const LinkConfig& cfg) {
    // 调用基类打开，建立数据面状态
    if (!TransportLinkAdapter::open(cfg)) {
        LOGE("rs422", "open", -1, "adapter base open failed");
        return false;
    }

    // 仅支持寄存器访问：要求已映射 user 寄存器空间
    auto& tp = transport();
    if (tp.getMappedBase() == nullptr || tp.getMappedLength() == 0) {
        LOGE("rs422", "open", -1, "register space unmapped");
        return false;
    }

    // 将 ICR 置 1，确保状态可读；其余寄存器配置由上层通过 ioctl 完成
    wr8(ICR_reg, 1);

    LOGI("rs422", "open", 0, "mtu=%u, regs=mmapped", getMTU());
    return true;
}

bool Yc7Device::close() {
    // 目前仅更新状态，资源由控制面自身负责关闭
    return TransportLinkAdapter::close();
}

bool Yc7Device::send_full(const uint8_t* data) {
    return send(data+1, data[0]);
}

bool Yc7Device::send(const uint8_t* data, uint32_t len) {
    auto& tp = transport();
    if (!data || len == 0) return true; // 空帧视为成功
    if (tp.getMappedBase() == nullptr) return false;

    // 检查发送就绪
    uint32_t stu = 0;
    if (!rd32(STU_reg, stu)) return false;
    if ((stu & STU_TX_READY_MASK) != STU_TX_READY_MASK) {
        // 设备忙
        LOGW("rs422", "send", 0, "device busy, stu=0x%02x", stu);
        return false;
    }

    // 写入长度寄存器
    uint16_t len_1 = len - 1;
    if (!wr8(LNL_reg, len_1 & 0xFF)) return false;
    if (!wr8(LNH_reg, (len_1 >> 8) & 0xFF)) return false;

    // 写入
    uint32_t bram_offset = 0;
    while (bram_offset < len) {
        uint32_t w = 0;
        // 组装 4 字节（不足补零）
        for (int i = 0; i < 4; ++i) {
            uint32_t idx = bram_offset + i;
            uint8_t b = (idx < len) ? data[idx] : 0;
            w |= (static_cast<uint32_t>(b) << (8 * i));
        }
        if (!wr32(SEND_BUF + bram_offset, w)) return false;
        bram_offset += 4;
    }

    // 写入发送命令
    if (!wr32(CMD_reg, CMD_TX)) return false;
    return true;
}

int32_t Yc7Device::receive_full(uint8_t* buf, uint32_t buf_size) {
    return 0;
}

int32_t Yc7Device::receive(uint8_t* buf, uint32_t buf_size) {
    return 0;
}

int32_t Yc7Device::receive_full(uint8_t* buf, uint32_t buf_size, uint32_t timeout_us) {
    return 0;
}

int32_t Yc7Device::receive(uint8_t* buf, uint32_t buf_size, uint32_t timeout_us) {
    return 0;
}

int Yc7Device::ioctl(uint32_t opcode, const void* in, size_t in_len, void* out, size_t out_len) {
    auto& tp = transport();
    if (tp.getMappedBase() == nullptr || tp.getMappedLength() == 0) {
        LOGE("rs422", "ioctl", -1, "register space unmapped");
        return -ENODEV;
    }

    switch (opcode) {
    case IOCTL_CONFIG: {
        // 校验输入参数
        if (in == nullptr || in_len != sizeof(Config)) {
            LOGE("rs422", "ioctl", -1, "invalid config payload");
            return -EINVAL;
        }

        const Config* cfg = reinterpret_cast<const Config*>(in);
        Config* cfg_out = reinterpret_cast<Config*>(out);

        // 遥测额外配置
        if (!wr8(YCL_reg, 0xAF))  { return -EIO; } 
        if (!wr8(YCH_reg, 0xFA))  { return -EIO; } 

        // 配置中断和回环
        if (!wr8(LPB_reg, cfg->lpb))  { return -EIO; } 
        if (!wr8(INT_reg, cfg->intr))  { return -EIO; } 
        if (!wr16(EVT_reg, cfg->evt))  { return -EIO; } 

        // 参考 rs422_config 的寄存器写序，并在每次写后短暂延时
        // UCR: UART 控制；MCR: 模式控制；BRSR: 波特率；ICR: 状态控制
        if (!wr8(UCR_reg, cfg->ucr))  { return -EIO; } 
        if (!wr8(MCR_reg, cfg->mcr))  { return -EIO; } 
        if (!wr8(BRSR_reg, cfg->brsr)){ return -EIO; } 
        if (!wr8(ICR_reg, cfg->icr))  { return -EIO; } // 超时返回空闲状态

        // 发送/接收头两个字节（低/高字节）
        if (!wr8(THL_reg, cfg->tx_head_lo)) { return -EIO; } 
        if (!wr8(THH_reg, cfg->tx_head_hi)) { return -EIO; } 
        if (!wr8(RHL_reg, cfg->rx_head_lo)) { return -EIO; } 
        if (!wr8(RHH_reg, cfg->rx_head_hi)) { return -EIO; } 

        LOGI("rs422", "ioctl", opcode,
             "configured ucr=0x%02x mcr=0x%02x brsr=0x%02x icr=0x%02x lpb=0x%02x intr=0x%02x tx_head=[0x%02x,0x%02x] rx_head=[0x%02x,0x%02x]",
             cfg->ucr, cfg->mcr, cfg->brsr, cfg->icr, cfg->lpb, cfg->intr,
             cfg->tx_head_lo, cfg->tx_head_hi, cfg->rx_head_lo, cfg->rx_head_hi);

        // 回读
        uint32_t ret1, ret2, ret3;
        if (!rd32(UCR_reg, ret1)) { return -EIO; } 
        if (!rd32(THL_reg, ret2)) { return -EIO; } 
        if (!rd32(LPB_reg, ret3)) { return -EIO; } 

        // 读取
        if (out != nullptr && out_len >= sizeof(Config)) {
            cfg_out->ucr = ret1 & 0xFF;
            cfg_out->mcr = ret1 >> 8 & 0xFF;
            cfg_out->brsr = ret1 >> 16 & 0xFF;
            cfg_out->icr = ret1 >> 24 & 0xFF;

            cfg_out->tx_head_lo = ret2 & 0xFF;
            cfg_out->tx_head_hi = ret2 >> 8 & 0xFF;
            cfg_out->rx_head_lo = ret2 >> 16 & 0xFF;
            cfg_out->rx_head_hi = ret2 >> 24 & 0xFF;

            cfg_out->lpb = ret3 & 0xFF;
            cfg_out->intr = ret3 >> 8 & 0xFF;

            LOGI("rs422", "ioctl", opcode,
                "read back  ucr=0x%02x mcr=0x%02x brsr=0x%02x icr=0x%02x lpb=0x%02x intr=0x%02x tx_head=[0x%02x,0x%02x] rx_head=[0x%02x,0x%02x]",
                cfg_out->ucr, cfg_out->mcr, cfg_out->brsr, cfg_out->icr, cfg_out->lpb, cfg_out->intr,
                cfg_out->tx_head_lo, cfg_out->tx_head_hi, cfg_out->rx_head_lo, cfg_out->rx_head_hi);
            // return -EIO;
        }

        return 0; // 成功
    }

    default:
        return -ENOSYS; // 未实现的操作码
    }
}

} // namespace Device
} // namespace PhysicalLayer
} // namespace MB_DDF