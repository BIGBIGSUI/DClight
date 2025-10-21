#include "slider.hpp"
#include <borealis/application.hpp>
#include <borealis/style.hpp>
#include <cmath>

namespace brls
{

Slider::Slider(std::string label, float min, float max, float initialValue)
    : label(label)
    , min(min)
    , max(max)
    , value(initialValue)
{
    // 注册左键操作
    this->registerAction("减小", Key::DLEFT, [this] { 
        float step = (this->max - this->min) / 20.0f; // 20个步骤
        float newValue = this->value - step;
        if (newValue < this->min) {
            newValue = this->max; // 循环到最大值
        }
        this->setValue(newValue);
        this->valueEvent.fire(newValue);
        return true;
    });
    
    // 注册右键操作
    this->registerAction("增大", Key::DRIGHT, [this] { 
        float step = (this->max - this->min) / 20.0f; // 20个步骤
        float newValue = this->value + step;
        if (newValue > this->max) {
            newValue = this->min; // 循环到最小值
        }
        this->setValue(newValue);
        this->valueEvent.fire(newValue);
        return true;
    });
}

void Slider::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, Style* style, FrameContext* ctx)
{
    // 绘制标签
    nvgFillColor(vg, a(ctx->theme->textColor));
    nvgFontSize(vg, 20);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontFaceId(vg, ctx->fontStash->regular);
    nvgText(vg, x + 20, y + 30, this->label.c_str(), nullptr);
    
    // 显示当前值
    std::string valueStr = std::to_string((int)this->value);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgText(vg, x + width - 20, y + 30, valueStr.c_str(), nullptr);
    
    // 绘制滑轨
    float sliderY = y + 60;
    float sliderWidth = width - 40.0f;
    float sliderHeight = 8.0f;
    
    // 背景轨道
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 20, sliderY, sliderWidth, sliderHeight, sliderHeight/2);
    nvgFillColor(vg, a(ctx->theme->listItemSeparatorColor));
    nvgFill(vg);
    
    // 当前值轨道
    float valueRatio = (this->value - this->min) / (this->max - this->min);
    if (valueRatio > 0)
    {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + 20, sliderY, sliderWidth * valueRatio, sliderHeight, sliderHeight/2);
        nvgFillColor(vg, a(ctx->theme->listItemValueColor));
        nvgFill(vg);
    }
    
    // 滑块手柄
    float handleX = x + 20 + sliderWidth * valueRatio - 15.0f;
    float handleY = sliderY - 11.0f;
    nvgBeginPath(vg);
    nvgCircle(vg, handleX + 15.0f, handleY + 15.0f, 15.0f);
    nvgFillColor(vg, a(ctx->theme->listItemValueColor));
    nvgFill(vg);
}

void Slider::layout(NVGcontext* vg, Style* style, FontStash* stash)
{
    this->setHeight(100);
}

View* Slider::getDefaultFocus()
{
    return this;
}

bool Slider::onClick()
{
    return true;
}

float Slider::getValue()
{
    return this->value;
}

void Slider::setValue(float value)
{
    this->value = std::max(this->min, std::min(this->max, value));
    this->invalidate(); // 重新绘制
}

Event<float>* Slider::getValueEvent()
{
    return &this->valueEvent;
}

}