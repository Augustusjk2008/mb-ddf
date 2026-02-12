# MB_DDF 单元测试

所有测试在AArch64目标板上执行。

## 快速开始

```powershell
# 构建并运行全部测试
.\test-deploy.ps1

# 仅运行Message相关测试
.\test-deploy.ps1 -TestFilter "Message*"

# 仅构建不上传
.\test-deploy.ps1 -BuildOnly

# 指定目标板
.\test-deploy.ps1 -RemoteHost 192.168.1.50
```

## 测试分类

| 目录 | 内容 | 硬件依赖 |
|------|------|----------|
| `unit/` | 纯内存测试（CRC32、Message、RingBuffer逻辑） | 无 |
| `component/` | 模块测试（用Mock隔离硬件） | Mock |
| `integration/` | 真实硬件测试（XDMA、CAN等） | 真实设备 |

## 添加新测试

1. 在对应目录创建 `test_xxx.cpp`
2. 包含 `<gtest/gtest.h>`
3. 使用 `TEST()` 或 `TEST_F()` 宏定义测试
4. 重新运行 `test-deploy.ps1`

示例：
```cpp
#include <gtest/gtest.h>

TEST(MyFeature, Scenario) {
    EXPECT_EQ(1 + 1, 2);
}
```
