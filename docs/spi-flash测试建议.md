基于Linux `spidev1.0` 驱动的 W25Q256JV 单线SPI（标准SPI，非QSPI）读写测试，核心思路是 **“先验证通信→再擦除→编程→读取验证”**，仅使用标准SPI指令（DI/DO分离，不涉及IO2/IO3），步骤清晰且无硬件改动，以下是完整测试方案：


## 一、测试前提与准备
### 1. 硬件准备
- 确保 W25Q256JV 与MCU/开发板的SPI引脚连接正确（标准SPI仅需4根线）：
  | W25Q256JV 引脚 | 功能       | 开发板SPI引脚（spidev1.0） |
  |----------------|------------|----------------------------|
  | /CS（Pad1）    | 片选       | SPI_CS1（根据板卡定义）    |
  | CLK（Pad6）    | 时钟       | SPI_CLK                    |
  | DI（IO0，Pad5）| 数据输入   | SPI_MOSI（主机输出→从机输入）|
  | DO（IO1，Pad2）| 数据输出   | SPI_MISO（主机输入→从机输出）|
  | VCC（Pad8）    | 电源       | 3.3V（2.7-3.6V范围内）     |
  | GND（Pad4）    | 地         | GND                        |
  | /WP（Pad3）    | 写保护     | 拉高（避免误触发写保护）    |
  | /HOLD（Pad7）  | 暂停       | 拉高（单线模式无需暂停功能）|
- 确认供电稳定（3.3V），避免因电压波动导致操作失败。

### 2. 软件准备
- 开发板已启用 `spidev1.0` 驱动（`ls /dev/spidev1.0` 可查看到设备节点）。
- 安装SPI测试依赖库（可选，C语言测试用）：`sudo apt-get install gcc make libspi-dev`。
- 核心约束：仅使用 **标准SPI指令**（单线模式），禁用QSPI相关操作（QE位保持默认0，无需配置）。


## 二、SPI参数配置（关键！）
W25Q256JV 对标准SPI的配置要求如下，必须在测试前通过 `spidev` 接口配置：
| 参数         | 配置值                  | 说明                                  |
|--------------|-------------------------|---------------------------------------|
| SPI模式      | Mode 0（推荐）或 Mode 3 | 芯片支持这两种模式，Mode0兼容性更好    |
| 时钟频率     | 读操作≤50MHz，写操作≤10MHz | 标准SPI读最大50MHz，写/擦除用低频率更稳定 |
| 位序         | MSB先行                 | 芯片强制要求（数据高位先传输）         |
| 位宽         | 8位                     | 所有指令均为8位字节传输               |
| 片选极性     | 低有效（默认）          | /CS拉低选中芯片，拉高释放              |

配置示例（C语言）：通过 `ioctl` 配置 `spidev` 参数，后续所有操作基于此配置。


## 三、测试流程（从简单到复杂，逐步验证）
### 核心逻辑：先验证SPI通信（读ID）→ 擦除目标扇区 → 编程数据 → 读取数据验证 → 对比结果
所有操作均遵循 **“指令+地址+数据”** 的时序（参考芯片手册8.2节指令描述），以下是具体步骤和代码实现。

### 步骤1：验证SPI通信（读JEDEC ID，最易操作）
#### 目的：确认SPI驱动、引脚连接正常，芯片响应正常
#### 指令说明（标准SPI，单线）：
- 指令码：`0x9F`（Read JEDEC ID）
- 时序：/CS拉低 → 发送指令`0x9F` → 芯片返回3字节数据（厂商ID+内存类型+容量）→ /CS拉高
- 预期返回值：`0xEF`（Winbond厂商ID） + `0x40`（内存类型） + `0x19`（256M-bit容量）

#### C语言代码片段（读JEDEC ID）：
```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <unistd.h>

// SPI设备节点
#define SPI_DEVICE "/dev/spidev1.0"
// SPI配置参数
#define SPI_MODE SPI_MODE_0  // Mode0
#define SPI_BITS_PER_WORD 8
#define SPI_SPEED_READ 10000000  // 10MHz（读操作可到50MHz，低频率更稳定）
#define SPI_SPEED_WRITE 5000000  // 5MHz（写/擦除用低频率）

int spi_fd;  // SPI设备文件描述符

// 1. SPI初始化（配置参数）
int spi_init() {
    // 打开SPI设备
    spi_fd = open(SPI_DEVICE, O_RDWR);
    if (spi_fd < 0) {
        perror("open spi device failed");
        return -1;
    }

    // 配置SPI模式
    int mode = SPI_MODE;
    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
        perror("set spi mode failed");
        return -1;
    }

    // 配置位宽（8位）
    int bits = SPI_BITS_PER_WORD;
    if (ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        perror("set spi bits failed");
        return -1;
    }

    // 配置读速度（可后续根据操作动态调整）
    unsigned int speed = SPI_SPEED_READ;
    if (ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("set spi speed failed");
        return -1;
    }

    printf("spi init success (mode:%d, bits:%d, speed:%d Hz)\n", mode, bits, speed);
    return 0;
}

// 2. 读JEDEC ID（验证通信）
int read_jedec_id(unsigned char *id_buf) {
    unsigned char tx_buf[4] = {0x9F, 0x00, 0x00, 0x00};  // 指令+3个dummy字节（芯片自动返回ID）
    unsigned char rx_buf[4] = {0};

    // SPI传输结构体（spidev标准接口）
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx_buf,
        .rx_buf = (unsigned long)rx_buf,
        .len = 4,  // 传输4字节（1指令+3数据）
        .speed_hz = SPI_SPEED_READ,
        .bits_per_word = SPI_BITS_PER_WORD,
        .cs_change = 0,  // 传输后不改变片选状态（最后手动拉高）
    };

    // 执行传输（/CS会自动拉低，传输完成后根据cs_change决定是否拉高）
    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        perror("read jedec id failed");
        return -1;
    }

    // 提取ID（rx_buf[1] = 厂商ID，rx_buf[2] = 内存类型，rx_buf[3] = 容量）
    id_buf[0] = rx_buf[1];
    id_buf[1] = rx_buf[2];
    id_buf[2] = rx_buf[3];

    // 打印结果
    printf("JEDEC ID: 0x%02X 0x%02X 0x%02X\n", id_buf[0], id_buf[1], id_buf[2]);
    return 0;
}

int main() {
    unsigned char jedec_id[3] = {0};

    // 初始化SPI
    if (spi_init() != 0) {
        return -1;
    }

    // 读JEDEC ID（通信验证）
    if (read_jedec_id(jedec_id) != 0) {
        close(spi_fd);
        return -1;
    }

    // 验证ID是否正确（W25Q256JV的标准JEDEC ID）
    if (jedec_id[0] == 0xEF && jedec_id[1] == 0x40 && jedec_id[2] == 0x19) {
        printf("SPI通信正常！芯片为W25Q256JV\n");
    } else {
        printf("SPI通信异常！请检查引脚连接或SPI配置\n");
        close(spi_fd);
        return -1;
    }

    close(spi_fd);
    return 0;
}
```

#### 编译与运行：
```bash
gcc -o spi_test spi_test.c
sudo ./spi_test  # 必须用root权限操作spidev
```
#### 预期输出：
```
spi init success (mode:0, bits:8, speed:10000000 Hz)
JEDEC ID: 0xEF 0x40 0x19
SPI通信正常！芯片为W25Q256JV
```


### 步骤2：读状态寄存器（确认芯片空闲）
#### 目的：后续写/擦除操作前，需确认芯片未忙（BUSY位=0）
#### 指令说明：
- 指令码：`0x05`（Read Status Register-1）
- 时序：/CS拉低 → 发送指令`0x05` → 芯片返回1字节状态寄存器数据 → /CS拉高
- 关键位：`BIT0（BUSY）`：1=芯片忙（擦除/编程中），0=空闲；`BIT1（WEL）`：1=写使能，0=写禁用

#### 新增代码函数（添加到上述代码中）：
```c
// 读状态寄存器-1（确认芯片是否空闲）
int read_status_reg1(unsigned char *status) {
    unsigned char tx_buf[2] = {0x05, 0x00};  // 指令+1个dummy字节
    unsigned char rx_buf[2] = {0};

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx_buf,
        .rx_buf = (unsigned long)rx_buf,
        .len = 2,
        .speed_hz = SPI_SPEED_READ,
        .bits_per_word = SPI_BITS_PER_WORD,
        .cs_change = 0,
    };

    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        perror("read status reg1 failed");
        return -1;
    }

    *status = rx_buf[1];
    printf("Status Register-1: 0x%02X (BUSY:%d, WEL:%d)\n", 
           *status, (*status)&0x01, (*status)&0x02);
    return 0;
}

// 等待芯片空闲（BUSY位清0）
int wait_chip_idle() {
    unsigned char status;
    do {
        read_status_reg1(&status);
        usleep(1000);  // 每1ms查询一次
    } while ((status & 0x01) == 1);  // BUSY=1时继续等待
    printf("芯片已空闲\n");
    return 0;
}
```

#### 在`main`函数中添加调用（通信验证后）：
```c
// 读状态寄存器，等待芯片空闲
unsigned char status;
read_status_reg1(&status);
wait_chip_idle();
```


### 步骤3：写测试（扇区擦除+页编程）
#### 核心注意事项：
1. W25Q256JV 是Flash芯片，**必须先擦除（数据变为0xFF）才能编程**，否则编程无效；
2. 写操作（擦除/编程）前必须执行 **写使能（0x06指令）**，否则芯片会忽略指令；
3. 单线模式下仅支持 **页编程（0x02指令）**，每次最多编程256字节（1页），地址不能跨页；
4. 测试选择 **4KB扇区（地址0x000000，扇区0）**，擦除范围小，不影响其他数据。

#### 新增写操作相关函数：
```c
// 写使能（所有写/擦除操作的前提）
int write_enable() {
    unsigned char tx_buf[1] = {0x06};  // 仅发送写使能指令
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx_buf,
        .rx_buf = 0,  // 无接收数据
        .len = 1,
        .speed_hz = SPI_SPEED_WRITE,
        .bits_per_word = SPI_BITS_PER_WORD,
        .cs_change = 0,
    };

    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        perror("write enable failed");
        return -1;
    }
    printf("已执行写使能\n");
    return 0;
}

// 扇区擦除（4KB，指令0x20）
int erase_sector(unsigned int sector_addr) {
    // 地址为24位（3字节），sector_addr为扇区起始地址（如0x000000）
    unsigned char tx_buf[4] = {0x20, 
                              (sector_addr >> 16) & 0xFF,  // A23-A16
                              (sector_addr >> 8) & 0xFF,   // A15-A8
                              sector_addr & 0xFF};        // A7-A0

    // 1. 写使能
    if (write_enable() != 0) {
        return -1;
    }

    // 2. 发送扇区擦除指令+地址
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx_buf,
        .rx_buf = 0,
        .len = 4,
        .speed_hz = SPI_SPEED_WRITE,
        .bits_per_word = SPI_BITS_PER_WORD,
        .cs_change = 0,
    };

    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        perror("erase sector failed");
        return -1;
    }
    printf("正在擦除扇区（地址0x%06X）...\n", sector_addr);

    // 3. 等待擦除完成（BUSY位清0）
    wait_chip_idle();
    printf("扇区擦除完成\n");
    return 0;
}

// 页编程（最多256字节，指令0x02）
int page_program(unsigned int addr, unsigned char *data, unsigned int len) {
    // 检查长度（不超过256字节，且不跨页）
    if (len > 256 || (addr & 0xFF) + len > 256) {
        printf("编程长度超出1页（256字节）或跨页，不支持\n");
        return -1;
    }

    // 构建发送缓冲区：指令（1字节）+ 24位地址（3字节）+ 数据（len字节）
    unsigned char *tx_buf = malloc(4 + len);
    tx_buf[0] = 0x02;
    tx_buf[1] = (addr >> 16) & 0xFF;  // A23-A16
    tx_buf[2] = (addr >> 8) & 0xFF;   // A15-A8
    tx_buf[3] = addr & 0xFF;          // A7-A0
    memcpy(&tx_buf[4], data, len);     // 待编程数据

    // 1. 写使能
    if (write_enable() != 0) {
        free(tx_buf);
        return -1;
    }

    // 2. 发送编程指令+地址+数据
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx_buf,
        .rx_buf = 0,
        .len = 4 + len,
        .speed_hz = SPI_SPEED_WRITE,
        .bits_per_word = SPI_BITS_PER_WORD,
        .cs_change = 0,
    };

    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        perror("page program failed");
        free(tx_buf);
        return -1;
    }
    printf("正在编程地址0x%06X（%d字节）...\n", addr, len);

    // 3. 等待编程完成
    wait_chip_idle();
    free(tx_buf);
    printf("页编程完成\n");
    return 0;
}
```


### 步骤4：读数据测试（验证编程结果）
#### 指令说明（读数据，0x03指令）：
- 指令码：`0x03`（Read Data）
- 时序：/CS拉低 → 发送指令`0x03` → 发送24位地址（3字节）→ 接收len字节数据 → /CS拉高

#### 新增读数据函数：
```c
// 读数据（指令0x03，标准SPI读）
int read_data(unsigned int addr, unsigned char *rx_data, unsigned int len) {
    // 构建发送缓冲区：指令（1字节）+ 24位地址（3字节）
    unsigned char tx_buf[4] = {0x03,
                              (addr >> 16) & 0xFF,
                              (addr >> 8) & 0xFF,
                              addr & 0xFF};

    // 接收缓冲区（len字节数据）
    memset(rx_data, 0, len);

    // SPI传输（先发送指令+地址，再接收数据）
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx_buf,
        .rx_buf = (unsigned long)rx_data,
        .len = 4 + len,  // 指令+地址（4字节）+ 数据（len字节）
        .speed_hz = SPI_SPEED_READ,
        .bits_per_word = SPI_BITS_PER_WORD,
        .cs_change = 0,
    };

    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        perror("read data failed");
        return -1;
    }

    // 打印读取结果
    printf("读取地址0x%06X：", addr);
    for (int i=0; i<len; i++) {
        printf("%02X ", rx_data[i]);
    }
    printf("\n");
    return 0;
}
```


### 步骤5：完整读写测试（主函数整合）
#### 整合后`main`函数：
```c
int main() {
    unsigned char jedec_id[3] = {0};
    unsigned char status;
    unsigned char write_data[] = {0x11, 0x22, 0x33, 0x44};  // 待编程数据（4字节）
    unsigned char read_data_buf[4] = {0};  // 读取数据缓冲区
    unsigned int test_addr = 0x000000;     // 测试地址（扇区0的起始地址）
    unsigned int test_sector = 0x000000;    // 测试扇区（4KB扇区0）

    // 1. 初始化SPI
    if (spi_init() != 0) {
        return -1;
    }

    // 2. 读JEDEC ID（通信验证）
    if (read_jedec_id(jedec_id) != 0 || 
        !(jedec_id[0] == 0xEF && jedec_id[1] == 0x40 && jedec_id[2] == 0x19)) {
        printf("SPI通信异常！\n");
        close(spi_fd);
        return -1;
    }

    // 3. 读状态寄存器，等待芯片空闲
    read_status_reg1(&status);
    wait_chip_idle();

    // 4. 擦除测试扇区（必须先擦除再编程）
    if (erase_sector(test_sector) != 0) {
        close(spi_fd);
        return -1;
    }

    // 5. 页编程（写入4字节数据）
    if (page_program(test_addr, write_data, sizeof(write_data)) != 0) {
        close(spi_fd);
        return -1;
    }

    // 6. 读数据（验证写入结果）
    if (read_data(test_addr, read_data_buf, sizeof(read_data_buf)) != 0) {
        close(spi_fd);
        return -1;
    }

    // 7. 对比写入和读取的数据
    int ret = memcmp(write_data, read_data_buf, sizeof(write_data));
    if (ret == 0) {
        printf("\n===== 读写测试成功！=====\n");
        printf("写入数据：");
        for (int i=0; i<sizeof(write_data); i++) printf("%02X ", write_data[i]);
        printf("\n读取数据：");
        for (int i=0; i<sizeof(read_data_buf); i++) printf("%02X ", read_data_buf[i]);
        printf("\n");
    } else {
        printf("\n===== 读写测试失败！=====\n");
    }

    close(spi_fd);
    return 0;
}
```


## 四、编译运行与预期结果
### 1. 编译：
```bash
gcc -o w25q256_test w25q256_test.c -lpthread  # 无需额外库，仅需标准C库
```

### 2. 运行（必须root权限）：
```bash
sudo ./w25q256_test
```

### 3. 预期输出：
```
spi init success (mode:0, bits:8, speed:10000000 Hz)
JEDEC ID: 0xEF 0x40 0x19
Status Register-1: 0x00 (BUSY:0, WEL:0)
芯片已空闲
已执行写使能
正在擦除扇区（地址0x000000）...
芯片已空闲
扇区擦除完成
已执行写使能
正在编程地址0x000000（4字节）...
芯片已空闲
页编程完成
读取地址0x000000：11 22 33 44 

===== 读写测试成功！=====
写入数据：11 22 33 44 
读取数据：11 22 33 44 
```


## 五、关键注意事项（避坑指南）
1. **SPI模式错误**：若读不到ID，先检查模式是否为Mode0/Mode3（多数开发板默认Mode0），模式错误会导致通信完全失败；
2. **写使能遗漏**：擦除/编程前必须执行`0x06`写使能，否则芯片会忽略指令（无报错但操作无效）；
3. **未擦除直接编程**：Flash未擦除时数据为非0xFF，编程无法覆盖，会导致读取数据异常（如全0xFF）；
4. **地址越界**：测试地址需在3字节地址范围内（0x000000~0x07FFFFFF，128Mb），超出需配置扩展地址寄存器（简单测试无需）；
5. **时钟频率过高**：写/擦除操作时钟超过10MHz可能导致数据丢失，建议写操作≤5MHz，读操作≤20MHz（兼容所有情况）；
6. **BUSY位未等待**：擦除（4KB扇区约50~400ms）和编程（约0.7~3ms）需要时间，未等BUSY位清0就进行下一步会导致操作失败。


## 六、后续扩展（可选）
- 若需读更大范围数据：修改`read_data`函数的`len`参数（最多连续读取整个Flash，地址自动递增）；
- 若需测试更多指令：可添加读唯一ID（0x4B指令）、芯片擦除（0xC7指令，谨慎使用，耗时80~400秒）；
- 若需调试：可在每个操作后读状态寄存器，查看WEL位（写使能是否生效）、BUSY位（操作是否执行）。

通过以上步骤，即可基于`spidev1.0`完成W25Q256JV的单线SPI读写验证，确认芯片和驱动均正常工作。