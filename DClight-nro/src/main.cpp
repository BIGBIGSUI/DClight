/*
    DClight 亮度调节程序
    通过滑块调节 config/DClight/config.ini 中的 brightness 值
*/

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

// INI文件配置
const char* CONFIG_DIR = "sdmc:/config/DClight";
const char* CONFIG_FILE = "sdmc:/config/DClight/config.ini";
const char* INI_SECTION = "DClight";
const char* INI_KEY = "brightness";

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

// 从INI文件读取亮度值
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

// 保存亮度值到INI文件
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

int main(int argc, char* argv[]) {
    // 初始化Switch系统库
    socketInitializeDefault();
    nxlinkStdio();

    // 初始化Borealis应用框架
    if (!brls::Application::init("DClight 亮度设置"))
    {
        brls::Logger::error("无法初始化Borealis应用程序");
        return EXIT_FAILURE;
    }

    // 设置日志级别
    brls::Logger::setLogLevel(brls::LogLevel::DEBUG);

    // 加载中文字体支持
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

    // 创建列表视图
    brls::List* list = new brls::List();
    
    // 添加说明标签
    brls::Label* description = new brls::Label(
        brls::LabelStyle::REGULAR,
        "调节 DClight 覆盖层亮度\n100 = 不暗化，0 = 最暗\n\n使用方向键左右调节亮度",
        true
    );
    list->addView(description);
    
    // 从配置文件读取当前亮度值
    int currentBrightness = loadBrightness();
    
    // 创建亮度滑块
    brls::Slider* brightnessSlider = new brls::Slider(
        "覆盖层亮度", 
        0.0f, 
        100.0f, 
        (float)currentBrightness
    );
    
    // 监听值改变事件，保存到INI文件
    brightnessSlider->getValueEvent()->subscribe([](float value) {
        int brightness = (int)value;
        brls::Logger::info("亮度调节为: {}", brightness);
        saveBrightness(brightness);
    });
    
    list->addView(brightnessSlider);
    
    // 添加当前配置信息
    brls::Label* configInfo = new brls::Label(
        brls::LabelStyle::SMALL,
        "配置文件: config/DClight/config.ini\n键名: brightness",
        true
    );
    list->addView(configInfo);

    // 创建应用框架
    brls::AppletFrame* frame = new brls::AppletFrame(true, true);
    frame->setTitle("DClight 亮度设置");
    frame->setContentView(list);
    
    // 推送主界面
    brls::Application::pushView(frame);

    // 运行应用主循环
    while (brls::Application::mainLoop());

    // 退出应用
    socketExit();
    
    return EXIT_SUCCESS;
}