#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/modify/MenuLayer.hpp>

using namespace geode::prelude;

class FrameCounterPopup : public Popup {
public:
    static FrameCounterPopup* create() {
        auto ret = new FrameCounterPopup();
        if (ret && ret->init(280.f, 190.f)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool setup() {
        auto winSize = m_size;

        auto title = CCLabelBMFont::create("Frame Counter", "goldFont.fnt");
        title->setPosition({winSize.width / 2, winSize.height - 25.f});
        title->setScale(0.7f);
        m_mainLayer->addChild(title);

        auto label = CCLabelBMFont::create("Popup is working, let's gooo!", "chatFont.fnt");
        label->setPosition({winSize.width / 2, winSize.height / 2});
        m_mainLayer->addChild(label);

        return true;
    }
};

class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }

        FrameCounterPopup::create()->show();

        return true;
    }
};