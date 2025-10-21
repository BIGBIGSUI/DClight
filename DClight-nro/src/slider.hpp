#pragma once

#include <borealis.hpp>

namespace brls
{

class Slider : public View
{
public:
    Slider(std::string label, float min, float max, float initialValue);
    
    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, Style* style, FrameContext* ctx) override;
    void layout(NVGcontext* vg, Style* style, FontStash* stash) override;
    View* getDefaultFocus() override;
    
    bool onClick();
    
    float getValue();
    void setValue(float value);
    
    Event<float>* getValueEvent();

private:
    std::string label;
    float min;
    float max;
    float value;
    
    Event<float> valueEvent;
};

}