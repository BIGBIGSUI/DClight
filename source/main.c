// 采用 libtesla 的绘制逻辑：VI 层、帧缓冲、RGBA4444、块线性 swizzle 与像素混合
// 标准头文件
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util/log.h"

// libnx 头文件
#include <switch.h>
#include <switch/display/framebuffer.h>
#include <switch/display/native_window.h>
#include <switch/services/sm.h>
#include <switch/runtime/devices/fs_dev.h>
// Applet type query for NV auto-selection
#include <switch/services/applet.h>
// NV 与 NVMAP/FENCE 以便显式初始化与日志
#include <switch/services/nv.h>
#include <switch/nvidia/map.h>
#include <switch/nvidia/fence.h>

// minIni for INI reading
#include "minIni.h"

// 覆盖 libnx 的弱符号以强制 NV 服务类型和 tmem 大小（避免卡住）
NvServiceType __attribute__((weak)) __nx_nv_service_type = NvServiceType_Application; // 默认 Auto 在 sysmodule 会选 System；强制走 nvdrv(u)
u32 __attribute__((weak)) __nx_nv_transfermem_size = 0x200000; // 将 tmem 从 8MB 降到 2MB，规避大内存问题

// 内部堆大小（按需调整）
#define INNER_HEAP_SIZE 0x400000

// 屏幕分辨率（与 tesla.hpp 对齐）
#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080

// 配置项（与 tesla cfg 对齐）
static u16 CFG_FramebufferWidth = 448;
static u16 CFG_FramebufferHeight = 720;
static u16 CFG_LayerWidth = 0;
static u16 CFG_LayerHeight = 0;
static u16 CFG_LayerPosX = 0;
static u16 CFG_LayerPosY = 0;

// Renderer 等价的状态
static ViDisplay g_display;
static ViLayer g_layer;
static Event g_vsyncEvent;
static NWindow g_window;
static Framebuffer g_framebuffer;
static void *g_currentFramebuffer = NULL;
static bool g_gfxInitialized = false;

// VI 层栈添加（tesla.hpp 使用的辅助函数）
static Result viAddToLayerStack(ViLayer *layer, ViLayerStack stack) {
    const struct {
        u32 stack;
        u64 layerId;
    } in = { stack, layer->layer_id };
    return serviceDispatchIn(viGetSession_IManagerDisplayService(), 6000, in);
}

// libnx 在 vi.c 中提供的弱符号：用于让 viCreateLayer 关联到已创建的 Managed Layer
extern u64 __nx_vi_layer_id;

// 颜色结构（4bit RGBA）
typedef struct { u8 r, g, b, a; } Color;

static inline u16 color_to_u16(Color c) {
    return (u16)((c.r & 0xF) | ((c.g & 0xF) << 4) | ((c.b & 0xF) << 8) | ((c.a & 0xF) << 12));
}

static inline Color color_from_u16(u16 raw) {
    Color c;
    c.r = (raw >> 0) & 0xF;
    c.g = (raw >> 4) & 0xF;
    c.b = (raw >> 8) & 0xF;
    c.a = (raw >> 12) & 0xF;
    return c;
}

// 像素混合（与 tesla.hpp 的 blendColor 一致）
static inline u8 blendColor(u8 src, u8 dst, u8 alpha) {
    u8 oneMinusAlpha = 0x0F - alpha;
    // 使用浮点以匹配 tesla.hpp 行为
    return (u8)((dst * alpha + src * oneMinusAlpha) / (float)0xF);
}

// 将 x,y 映射为块线性帧缓冲中的偏移（与 tesla.hpp getPixelOffset 一致）
static inline u32 getPixelOffset(s32 x, s32 y) {
    // 边界由调用者保证，这里直接映射
    u32 tmpPos = ((y & 127) / 16) + (x / 32 * 8) + ((y / 16 / 8) * (((CFG_FramebufferWidth / 2) / 16 * 8)));
    tmpPos *= 16 * 16 * 4;
    tmpPos += ((y % 16) / 8) * 512 + ((x % 32) / 16) * 256 + ((y % 8) / 2) * 64 + ((x % 16) / 8) * 32 + (y % 2) * 16 + (x % 8) * 2;
    return tmpPos / 2;
}

// 绘制基本原语
static inline void setPixel(s32 x, s32 y, Color color) {
    if (x < 0 || y < 0 || x >= (s32)CFG_FramebufferWidth || y >= (s32)CFG_FramebufferHeight || g_currentFramebuffer == NULL) return;
    u32 offset = getPixelOffset(x, y);
    ((u16*)g_currentFramebuffer)[offset] = color_to_u16(color);
}

static inline void setPixelBlendDst(s32 x, s32 y, Color color) {
    if (x < 0 || y < 0 || x >= (s32)CFG_FramebufferWidth || y >= (s32)CFG_FramebufferHeight || g_currentFramebuffer == NULL) return;
    u32 offset = getPixelOffset(x, y);
    Color src = color_from_u16(((u16*)g_currentFramebuffer)[offset]);
    Color dst = color;
    Color out = {0,0,0,0};
    out.r = blendColor(src.r, dst.r, dst.a);
    out.g = blendColor(src.g, dst.g, dst.a);
    out.b = blendColor(src.b, dst.b, dst.a);
    // alpha 叠加并限制到 0xF
    u16 sumA = (u16)dst.a + (u16)src.a;
    out.a = (sumA > 0xF) ? 0xF : (u8)sumA;
    setPixel(x, y, out);
}

static inline void drawRect(s32 x, s32 y, s32 w, s32 h, Color color) {
    s32 x2 = x + w;
    s32 y2 = y + h;
    if (x2 < 0 || y2 < 0) return;
    if (x >= (s32)CFG_FramebufferWidth || y >= (s32)CFG_FramebufferHeight) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > (s32)CFG_FramebufferWidth) x2 = CFG_FramebufferWidth;
    if (y2 > (s32)CFG_FramebufferHeight) y2 = CFG_FramebufferHeight;
    for (s32 xi = x; xi < x2; ++xi) {
        for (s32 yi = y; yi < y2; ++yi) {
            setPixelBlendDst(xi, yi, color);
        }
    }
}

static inline void fillScreen(Color color) {
    drawRect(0, 0, CFG_FramebufferWidth, CFG_FramebufferHeight, color);
}

// 无混合的整屏填充（直接写入像素，保证底色和 alpha 精确）
static inline void fillScreenSolid(Color color) {
    if (g_currentFramebuffer == NULL) return;
    for (s32 yi = 0; yi < (s32)CFG_FramebufferHeight; ++yi) {
        for (s32 xi = 0; xi < (s32)CFG_FramebufferWidth; ++xi) {
            setPixel(xi, yi, color);
        }
    }
}

// 读取亮度(0-100)，映射为覆盖层alpha(0-15)，值越低越亮度越暗
static u8 load_dim_alpha_from_ini(void) {
    const char *ini_path = "sdmc:/config/DClight/config.ini";

    // 允许键位于根或节 [DClight]/[overlay]
    long brightness = ini_getl(NULL, "brightness", -1, ini_path);
    if (brightness < 0) brightness = ini_getl("DClight", "brightness", -1, ini_path);
    if (brightness < 0) brightness = ini_getl("overlay", "brightness", -1, ini_path);

    long alpha_override = ini_getl(NULL, "alpha", -1, ini_path);
    if (alpha_override < 0) alpha_override = ini_getl("DClight", "alpha", -1, ini_path);
    if (alpha_override < 0) alpha_override = ini_getl("overlay", "alpha", -1, ini_path);

    u8 alpha;
    if (brightness >= 0) {
        if (brightness < 0) brightness = 0;
        if (brightness > 100) brightness = 100;
        // 100 亮度 -> alpha=0（无暗化）；0 亮度 -> alpha=15（最暗）
        alpha = (u8)(((100 - brightness) * 15) / 100);
    } else if (alpha_override >= 0) {
        if (alpha_override < 0) alpha_override = 0;
        if (alpha_override > 15) alpha_override = 15;
        alpha = (u8)alpha_override;
    } else {
        // 默认：不暗化
        alpha = 0;
    }

    log_info("ini brightness=%ld, alpha_override=%ld -> alpha=%u (path=%s)", brightness, alpha_override, alpha, ini_path);
    return alpha;
}

// 帧控制
static inline void startFrame(void) {
    g_currentFramebuffer = framebufferBegin(&g_framebuffer, NULL);
}

static inline void endFrame(void) {
    eventWait(&g_vsyncEvent, UINT64_MAX);
    framebufferEnd(&g_framebuffer);
    g_currentFramebuffer = NULL;
}

// 图形初始化与释放（移植 tesla Renderer::init/exit 的核心）
static Result gfx_init(void) {
    // 设置 Layer 为全屏覆盖
    CFG_LayerWidth  = SCREEN_WIDTH;
    CFG_LayerHeight = SCREEN_HEIGHT;
    CFG_LayerPosX = 0;
    CFG_LayerPosY = 0;

    log_info("viInitialize(ViServiceType_Manager)...");
    Result rc = viInitialize(ViServiceType_Manager);
    if (R_FAILED(rc)) return rc;

    log_info("viOpenDefaultDisplay...");
    rc = viOpenDefaultDisplay(&g_display);
    if (R_FAILED(rc)) return rc;

    log_info("viGetDisplayVsyncEvent...");
    rc = viGetDisplayVsyncEvent(&g_display, &g_vsyncEvent);
    if (R_FAILED(rc)) return rc;

    // 确保显示全局 Alpha 为不透明
    log_info("viSetDisplayAlpha(1.0f)...");
    viSetDisplayAlpha(&g_display, 1.0f);

    log_info("viCreateManagedLayer...");
    rc = viCreateManagedLayer(&g_display, (ViLayerFlags)0, 0, &__nx_vi_layer_id);
    if (R_FAILED(rc)) return rc;

    log_info("viCreateLayer...");
    rc = viCreateLayer(&g_display, &g_layer);
    if (R_FAILED(rc)) return rc;

    log_info("viSetLayerScalingMode(FitToLayer)...");
    rc = viSetLayerScalingMode(&g_layer, ViScalingMode_FitToLayer);
    if (R_FAILED(rc)) return rc;

    s32 layerZ = 250;
    log_info("viSetLayerZ(%d)...", layerZ);
    rc = viSetLayerZ(&g_layer, layerZ);
    if (R_FAILED(rc)) return rc;

    // 保守策略：仅添加到必要的图层栈（Default + Screenshot）
    log_info("viAddToLayerStack(Default and Screenshot)...");
    rc = viAddToLayerStack(&g_layer, ViLayerStack_Default);
    if (R_FAILED(rc)) return rc;
    rc = viAddToLayerStack(&g_layer, ViLayerStack_Screenshot);
    if (R_FAILED(rc)) return rc;

    log_info("viSetLayerSize(%u,%u)...", CFG_LayerWidth, CFG_LayerHeight);
    rc = viSetLayerSize(&g_layer, CFG_LayerWidth, CFG_LayerHeight);
    if (R_FAILED(rc)) return rc;
    log_info("viSetLayerPosition(%u,%u) 屏幕居中", CFG_LayerPosX, CFG_LayerPosY);
    rc = viSetLayerPosition(&g_layer, CFG_LayerPosX, CFG_LayerPosY);
    if (R_FAILED(rc)) return rc;

    log_info("nwindowCreateFromLayer...");
    rc = nwindowCreateFromLayer(&g_window, &g_layer);
    if (R_FAILED(rc)) return rc;

    log_info("framebufferCreate(%u,%u,RGBA_4444,2)...", CFG_FramebufferWidth, CFG_FramebufferHeight);
    rc = framebufferCreate(&g_framebuffer, &g_window, CFG_FramebufferWidth, CFG_FramebufferHeight, PIXEL_FORMAT_RGBA_4444, 2);
    if (R_FAILED(rc)) return rc;

    g_gfxInitialized = true;
    log_info("gfx_init 完成");
    return 0;
}

static void gfx_exit(void) {
    if (!g_gfxInitialized) return;
    
    log_info("开始清理图形资源...");
    
    // 清理图形相关资源
    framebufferClose(&g_framebuffer);
    nwindowClose(&g_window);
    
    // 安全清理VI资源，避免与其他 overlay 冲突（仿照 pop-windows-main）
    log_info("安全清理VI资源...");
    
    // 检查VI服务是否仍然可用
    Result rc = 0;
    
    // 尝试销毁Managed Layer（容错处理）
    rc = viDestroyManagedLayer(&g_layer);
    if (R_FAILED(rc)) {
        log_info("viDestroyManagedLayer失败 (可能已被其他程序清理): 0x%x", rc);
    }
    
    // 尝试关闭Display（容错处理）
    rc = viCloseDisplay(&g_display);
    if (R_FAILED(rc)) {
        log_info("viCloseDisplay失败 (可能已被其他程序清理): 0x%x", rc);
    }
    
    eventClose(&g_vsyncEvent);
    
    // 最后尝试退出VI服务
    // 如果其他程序已经调用了viExit()，这里的调用可能会失败，但不会导致程序崩溃
    viExit();
    
    g_gfxInitialized = false;
    
    log_info("图形资源清理完成");
}

#ifdef __cplusplus
extern "C" {
#endif

// 后台程序：不使用 Applet 环境
u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

// 配置 newlib 堆（使 malloc/free 可用）
void __libnx_initheap(void)
{
    static u8 inner_heap[INNER_HEAP_SIZE];
    extern void *fake_heap_start;
    extern void *fake_heap_end;
    fake_heap_start = inner_heap;
    fake_heap_end = inner_heap + sizeof(inner_heap);
}

// 必要服务初始化（完全仿照 pop-windows-main 的严格错误处理）
void __appInit(void)
{
    log_info("应用程序初始化开始...");
    
    Result rc = 0;
    
    // 基础服务初始化
    rc = smInitialize();
    if (R_FAILED(rc)) {
        log_error("smInitialize失败: 0x%x", rc);
        fatalThrow(rc);
    }
    
    rc = fsInitialize();
    if (R_FAILED(rc)) {
        log_error("fsInitialize失败: 0x%x", rc);
        fatalThrow(rc);
    }
    
    fsdevMountSdmc();
    
    // 其他服务初始化
    rc = hidInitialize();
    if (R_FAILED(rc)) {
        log_error("hidInitialize失败: 0x%x", rc);
        fatalThrow(rc);
    }
    
    log_info("应用程序初始化完成");
}

// 服务释放（完全仿照 pop-windows-main 的清理顺序）
void __appExit(void)
{
    log_info("应用程序退出开始...");
    
    // 优先清理图形资源，避免与其他叠加层冲突
    gfx_exit();
    
    // 清理其他服务
    hidExit();
    
    // 最后清理基础服务
    fsdevUnmountAll();
    fsExit();
    smExit();
    
    log_info("应用程序退出完成");
}

#ifdef __cplusplus
}
#endif

// 主入口：初始化绘制并执行一次演示帧，然后进入后台循环
int main(int argc, char *argv[])
{
    log_info("后台程序启动（移植 tesla 绘制逻辑）");

    Result rc = gfx_init();
    if (R_SUCCEEDED(rc)) {
        log_info("进入实时亮度调整循环...");
    } else {
        log_error("图形初始化失败: 0x%x", rc);
    }

    // 后台循环：实时读取 INI 并调整覆盖层透明度
    while (true) {
        if (g_gfxInitialized) {
            startFrame();
            u8 dimAlpha = load_dim_alpha_from_ini();
            fillScreenSolid((Color){0, 0, 0, dimAlpha});
            endFrame();
        }
        // 500ms 刷新一次亮度
        svcSleepThread(500000000ULL);
    }

    gfx_exit();
    return 0;
}