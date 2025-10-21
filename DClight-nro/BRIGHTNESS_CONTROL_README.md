# DClight 亮度控制程序

这是一个用于调节DClight覆盖层亮度的Switch homebrew程序，使用滑块组件来修改配置文件。

## 功能说明

- 通过滑块实时调节DClight覆盖层亮度（0-100）
- 自动读取和保存配置到 `config/DClight/config.ini`
- 100 = 不暗化，0 = 最暗

## 配置文件

### 位置
```
/config/DClight/config.ini
```

### 格式
```ini
[DClight]
# DClight 覆盖层亮度配置（0-100）
# 100 不暗化；0 最暗
brightness=80
```

## 文件结构

```
src/
├── main.cpp       - 主程序，包含INI读写逻辑
├── slider.cpp     - 滑块组件实现
└── slider.hpp     - 滑块组件头文件

config/DClight/
└── config.ini     - 示例配置文件
```

## 工作原理

### 1. 启动时读取配置
```cpp
int loadBrightness() {
    // 从INI文件读取brightness值，默认80
    long brightness = ini_getl("DClight", "brightness", 80, CONFIG_FILE);
    return (int)brightness;
}
```

### 2. 滑块值改变时保存
```cpp
brightnessSlider->getValueEvent()->subscribe([](float value) {
    int brightness = (int)value;
    saveBrightness(brightness);  // 保存到INI文件
});
```

### 3. 保存配置到INI文件
```cpp
void saveBrightness(int brightness) {
    // 使用minIni库写入配置
    ini_putl("DClight", "brightness", brightness, CONFIG_FILE);
}
```

## 使用的库

### minIni
项目使用 `lib/minIni-nx` 库进行INI文件读写：

**读取配置：**
```cpp
long ini_getl(const char* Section, const char* Key, long DefValue, const char* Filename);
```

**写入配置：**
```cpp
int ini_putl(const char* Section, const char* Key, long Value, const char* Filename);
```

### borealis
使用borealis UI框架创建界面：
- `brls::List` - 列表容器
- `brls::Label` - 文本标签
- `brls::Slider` - 自定义滑块组件
- `brls::AppletFrame` - 应用框架

## 编译步骤

```bash
# 清理旧的编译文件
make clean

# 编译生成NRO文件
make
```

编译成功后会生成：
- `DClight-Brightness.nro` - 可在Switch上运行的文件
- `DClight-Brightness.elf` - ELF可执行文件
- `DClight-Brightness.nacp` - 应用元数据

## 运行效果

1. 启动程序后会显示当前亮度值（从配置文件读取）
2. 使用方向键左右调节滑块
3. 每次调节后自动保存到配置文件
4. 程序会在SD卡上自动创建 `/config/DClight` 目录

## 界面布局

```
┌─────────────────────────────────┐
│  DClight 亮度设置               │
├─────────────────────────────────┤
│ 调节 DClight 覆盖层亮度         │
│ 100 = 不暗化，0 = 最暗          │
│                                 │
│ 使用方向键左右调节亮度           │
├─────────────────────────────────┤
│ 覆盖层亮度               80     │
│ ━━━━━━━━●━━━━━━━━━━━━━          │
├─────────────────────────────────┤
│ 配置文件: config/DClight/...    │
│ 键名: brightness                │
└─────────────────────────────────┘
```

## 核心代码说明

### INI文件路径配置
```cpp
const char* CONFIG_DIR = "sdmc:/config/DClight";
const char* CONFIG_FILE = "sdmc:/config/DClight/config.ini";
const char* INI_SECTION = "DClight";
const char* INI_KEY = "brightness";
```

### 自动创建配置目录
```cpp
void createConfigDir() {
    struct stat st = {0};
    if (stat("/config", &st) == -1) {
        mkdir("/config", 0755);
    }
    if (stat("/config/DClight", &st) == -1) {
        mkdir("/config/DClight", 0755);
    }
}
```

### 滑块配置
- **标签**: "覆盖层亮度"
- **最小值**: 0
- **最大值**: 100
- **步进**: 5（20个步进）
- **初始值**: 从配置文件读取

## Makefile配置

```makefile
TARGET    := DClight-Brightness           # 输出文件名
SOURCES   := src lib/minIni-nx/source    # 包含minIni源文件
INCLUDES  := src lib/minIni-nx/include   # 包含minIni头文件
APP_TITLE := DClight Brightness          # 应用标题
```

## 技术要点

1. **外部C库调用**: 使用 `extern "C"` 包装minIni库
2. **文件系统操作**: 使用 `stat()` 和 `mkdir()` 创建目录
3. **事件驱动**: 滑块值改变触发保存操作
4. **错误处理**: 确保亮度值在0-100范围内
5. **中文支持**: 加载Switch系统中文字体

## 调试日志

程序运行时会输出以下日志：
- 启动时读取的亮度值
- 每次调节后保存的亮度值
- 保存成功或失败的状态

通过nxlink可以查看这些日志信息。

## 注意事项

1. 程序会自动创建配置目录，无需手动创建
2. 如果配置文件不存在，会使用默认值80
3. 亮度值会自动限制在0-100范围内
4. 每次调节滑块都会立即保存到配置文件
5. Switch必须能够写入SD卡

## 开发者信息

- 基于borealis UI框架
- 使用minIni进行配置文件管理
- 自定义滑块组件实现
- 支持Nintendo Switch homebrew环境

