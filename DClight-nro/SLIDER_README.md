# 滑块演示程序

这是一个简化版的Nintendo Switch homebrew程序，只包含滑块组件。

## 项目结构

```
src/
├── main.cpp       - 主程序入口，创建并展示滑块
├── slider.cpp     - 滑块组件实现
└── slider.hpp     - 滑块组件头文件
```

## 滑块实现说明

### 核心文件
- **slider.hpp**: 定义Slider类，继承自borealis的View类
- **slider.cpp**: 实现滑块的绘制、布局和交互逻辑
- **main.cpp**: 创建应用界面，实例化多个滑块并添加到列表中

### 滑块特性

1. **可视化元素**:
   - 显示标签文本（左上角）
   - 显示当前数值（右上角）
   - 背景轨道（灰色）
   - 活动轨道（高亮色，显示当前值）
   - 圆形滑块手柄

2. **交互功能**:
   - 按左方向键：减小值（20步进）
   - 按右方向键：增大值（20步进）
   - 值超出范围时循环到另一端
   - 值改变时触发事件回调

3. **可配置参数**:
   - 标签文本
   - 最小值
   - 最大值
   - 初始值

### 使用示例

```cpp
// 创建滑块：标签、最小值、最大值、初始值
brls::Slider* slider = new brls::Slider("音量", 0.0f, 100.0f, 50.0f);

// 监听值改变事件
slider->getValueEvent()->subscribe([](float value) {
    brls::Logger::info("值改变: {}", (int)value);
});

// 添加到列表
list->addView(slider);
```

## 编译方法

### 环境要求
- devkitPro
- libnx
- borealis库（已包含在lib/目录）

### 编译步骤

```bash
# 在项目根目录执行
make clean
make
```

### 输出文件
- `SliderDemo.nro` - 可在Switch上运行的homebrew文件
- `SliderDemo.elf` - ELF可执行文件
- `SliderDemo.nacp` - Switch应用元数据

## 关键技术点

### 1. 自定义View组件
滑块继承自`brls::View`，需要实现：
- `draw()` - 绘制组件
- `layout()` - 设置布局尺寸
- `getDefaultFocus()` - 设置默认焦点

### 2. NanoVG绘图
使用NanoVG库进行2D绘制：
- `nvgText()` - 绘制文本
- `nvgRoundedRect()` - 绘制圆角矩形
- `nvgCircle()` - 绘制圆形
- `nvgFillColor()` - 设置填充颜色

### 3. 事件系统
使用borealis的Event系统：
- `Event<float> valueEvent` - 定义值改变事件
- `valueEvent.fire(value)` - 触发事件
- `getValueEvent()->subscribe(callback)` - 订阅事件

### 4. 输入处理
通过`registerAction()`注册按键响应：
```cpp
this->registerAction("减小", Key::DLEFT, [this] { 
    // 处理左键
    return true;
});
```

## Makefile说明

### 关键配置
```makefile
TARGET    := SliderDemo                    # 输出文件名
SOURCES   := src lib/minIni-nx/source     # 源文件目录（已移除utils）
INCLUDES  := src lib/minIni-nx/include    # 头文件目录
APP_TITLE := Slider Demo                   # 应用标题
```

### 编译流程
1. 复制romfs资源（borealis字体和图标）
2. 编译源文件（main.cpp, slider.cpp等）
3. 链接生成ELF文件
4. 打包为.nro homebrew格式

## 运行效果

程序启动后显示三个滑块：
1. **音量滑块**: 0-100, 初始值50
2. **亮度滑块**: 0-100, 初始值75
3. **速度滑块**: 0-200, 初始值100

使用方向键左右调节各个滑块的值，值改变时会在日志中输出。

## 已删除的组件

为了简化项目，已删除以下文件：
- about_tab.cpp/h - 关于页面
- auto_backup_setting_tab.cpp/h - 自动备份设置
- backup_log_tab.cpp/h - 备份记录
- ftp_setting_tab.cpp/h - FTP设置
- main_frame.cpp/h - 主框架（TabFrame）
- utils.cpp/h - 工具函数
- webdav.cpp/h - WebDAV功能

现在只保留了滑块相关的核心代码。

