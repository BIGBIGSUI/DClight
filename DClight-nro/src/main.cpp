/* DClight 亮度调节：通过滑块修改 config/DClight/config.ini 里的 brightness */

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>

#include <borealis.hpp>
#include <switch.h>

extern "C" {
#include <minIni.h>
}

#include "slider.hpp"

// INI 配置
const char* CONFIG_DIR = "sdmc:/config/DClight";
const char* CONFIG_FILE = "sdmc:/config/DClight/config.ini";
const char* INI_SECTION = "DClight";
const char* INI_KEY = "brightness";

// Sysmodule TID
const u64 TID_DCLIGHT = 0x0000000002052918;  // DClight 调光 sysmodule
const u64 TID_OTHER_DIM = 0x420000000007E51A; // 另一个调光模块

// 创建配置目录
void createConfigDir() {
    struct stat st = {0};
    if (stat("/config", &st) == -1) {
        mkdir("/config", 0755);
    }
    if (stat("/config/DClight", &st) == -1) {
        mkdir("/config/DClight", 0755);
    }
}

// 读取亮度
int loadBrightness() {
    createConfigDir();
    
    // 读取配置，默认值80
    long brightness = ini_getl(INI_SECTION, INI_KEY, 80, CONFIG_FILE);
    
    // 确保值在有效范围内
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;
    
    brls::Logger::info("从配置文件读取亮度: {}", (int)brightness);
    return (int)brightness;
}

// 保存亮度
void saveBrightness(int brightness) {
    createConfigDir();
    
    // 确保值在有效范围内
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;
    
    // 写入配置
    int result = ini_putl(INI_SECTION, INI_KEY, brightness, CONFIG_FILE);
    
    if (result) {
        brls::Logger::info("亮度值已保存: {}", brightness);
    } else {
        brls::Logger::error("保存亮度值失败");
    }
}

// 检测 sysmodule 是否在运行
bool isSysmoduleRunning(u64 tid) {
    Result rc;
    u64 pid = 0;
    
    // 初始化 pmdmnt 服务
    rc = pmdmntInitialize();
    if (R_FAILED(rc)) {
        brls::Logger::error("pmdmnt 初始化失败: 0x{:X}", rc);
        return false;
    }
    
    // 尝试获取该 TID 对应的进程 ID
    rc = pmdmntGetProcessId(&pid, tid);
    pmdmntExit();
    
    if (R_SUCCEEDED(rc) && pid != 0) {
        brls::Logger::info("TID {:016X} 正在运行 (PID: {})", tid, pid);
        return true;
    } else {
        brls::Logger::info("TID {:016X} 未运行", tid);
        return false;
    }
}

// 停止 sysmodule
bool stopSysmodule(u64 tid) {
    Result rc;
    
    // 先检查是否在运行
    if (!isSysmoduleRunning(tid)) {
        brls::Logger::info("TID {:016X} 未运行，无需停止", tid);
        return true;
    }
    
    // 初始化 pmshell 服务
    rc = pmshellInitialize();
    if (R_FAILED(rc)) {
        brls::Logger::error("pmshell 初始化失败: 0x{:X}", rc);
        return false;
    }
    
    // 终止程序
    rc = pmshellTerminateProgram(tid);
    pmshellExit();
    
    if (R_SUCCEEDED(rc)) {
        brls::Logger::info("成功停止 TID {:016X}", tid);
        return true;
    } else {
        brls::Logger::error("停止 TID {:016X} 失败: 0x{:X}", tid, rc);
        return false;
    }
}

// 启动 sysmodule
bool startSysmodule(u64 tid) {
    Result rc;
    u64 pid = 0;
    
    // 先检查是否已经在运行
    if (isSysmoduleRunning(tid)) {
        brls::Logger::info("TID {:016X} 已在运行，无需启动", tid);
        return true;
    }
    
    // 初始化 pmshell 服务
    rc = pmshellInitialize();
    if (R_FAILED(rc)) {
        brls::Logger::error("pmshell 初始化失败: 0x{:X}", rc);
        return false;
    }
    
    // 构造程序位置结构体（与 Hekate-Toolbox 一致）
    NcmProgramLocation location{};
    location.program_id = tid;
    location.storageID = NcmStorageId_None;
    
    // 启动程序（flags=0 表示常规启动）
    rc = pmshellLaunchProgram(0, &location, &pid);
    pmshellExit();
    
    if (R_SUCCEEDED(rc)) {
        brls::Logger::info("成功启动 TID {:016X} (PID: {})", tid, pid);
        return true;
    } else {
        brls::Logger::error("启动 TID {:016X} 失败: 0x{:X}", tid, rc);
        return false;
    }
}

// 开关调光：在 DClight 和另一模块间切换
void toggleDimming(bool enableDClight) {
    if (enableDClight) {
        brls::Logger::info("正在启用 DClight 调光...");
        // 先停止另一个调光模块
        stopSysmodule(TID_OTHER_DIM);
        svcSleepThread(200000000ULL); // 等待 200ms
        // 再启动 DClight
        startSysmodule(TID_DCLIGHT);
    } else {
        brls::Logger::info("正在禁用 DClight 调光...");
        // 先停止 DClight
        stopSysmodule(TID_DCLIGHT);
        svcSleepThread(200000000ULL); // 等待 200ms
        // 再启动另一个调光模块
        startSysmodule(TID_OTHER_DIM);
    }
}

int main(int argc, char* argv[]) {
    // 初始化系统
    socketInitializeDefault();
    nxlinkStdio();

    // 初始化 Borealis
    if (!brls::Application::init("DClight 亮度设置"))
    {
        brls::Logger::error("无法初始化Borealis应用程序");
        return EXIT_FAILURE;
    }

    // 日志级别
    brls::Logger::setLogLevel(brls::LogLevel::DEBUG);

    // 加载中文字体
    PlFontData font;
    Result rc = plGetSharedFontByType(&font, PlSharedFontType_ChineseSimplified);
    if (R_SUCCEEDED(rc)) 
    {
        brls::Logger::info("添加中文字体支持");
        int chineseFont = brls::Application::loadFontFromMemory("chinese", font.address, font.size, false);
        nvgAddFallbackFontId(brls::Application::getNVGContext(), brls::Application::getFontStash()->regular, chineseFont);
    } 
    else 
    {
        brls::Logger::error("无法加载中文字体");
    }

    // 列表视图
    brls::List* list = new brls::List();
    
    // 说明
    brls::Label* description = new brls::Label(
        brls::LabelStyle::REGULAR,
        "调节覆盖层亮度 (100=不暗, 0=最暗)\n方向键/摇杆 左右调节",
        true
    );
    list->addView(description);
    
    // 调光开关（手动管理状态）
    bool isDClightRunning = isSysmoduleRunning(TID_DCLIGHT);
    
    brls::ListItem* toggleItem = new brls::ListItem("启用 DClight 调光");
    toggleItem->setValue(isDClightRunning ? "开启" : "关闭");
    
    // 当前状态
    static bool currentDimmingState = isDClightRunning;
    
    // 点击切换
    toggleItem->getClickEvent()->subscribe([toggleItem](brls::View* view) {
        // 切换状态
        currentDimmingState = !currentDimmingState;
        
        // 更新显示
        toggleItem->setValue(currentDimmingState ? "开启" : "关闭");
        
        // 执行切换逻辑
        brls::Logger::info("调光开关切换为: {}", currentDimmingState ? "开启" : "关闭");
        toggleDimming(currentDimmingState);
    });
    
    list->addView(toggleItem);
    
    // 分隔线
    list->addView(new brls::Header("亮度设置"));
    
    // 读取当前亮度
    int currentBrightness = loadBrightness();
    
    // 亮度滑块
    brls::Slider* brightnessSlider = new brls::Slider(
        "覆盖层亮度", 
        0.0f, 
        100.0f, 
        (float)currentBrightness
    );
    
    // 监听变化并保存
    brightnessSlider->getValueEvent()->subscribe([](float value) {
        int brightness = (int)value;
        brls::Logger::info("亮度调节为: {}", brightness);
        saveBrightness(brightness);
    });
    
    list->addView(brightnessSlider);
    
    // 配置信息
    brls::Label* configInfo = new brls::Label(
        brls::LabelStyle::SMALL,
        "配置: config/DClight/config.ini / brightness",
        true
    );
    list->addView(configInfo);

    // 应用框架
    brls::AppletFrame* frame = new brls::AppletFrame(true, true);
    frame->setTitle("DClight 亮度设置");
    frame->setContentView(list);
    
    // 推送主界面
    brls::Application::pushView(frame);

    // 主循环
    while (brls::Application::mainLoop());

    // 退出
    socketExit();
    
    return EXIT_SUCCESS;
}