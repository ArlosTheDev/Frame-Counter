#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

using namespace geode::prelude;

// 1. Define your custom popup class
class FrameCounterPopup : public Popup<> {
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

    bool setup() override {
        // Center of the popup window
        auto winSize = m_size;

        // Add a title label
        auto title = CCLabelBMFont::create("Frame Counter", "goldFont.fnt");
        title->setPosition({winSize.width / 2, winSize.height - 25.f});
        title->setScale(0.7f);
        m_mainLayer->addChild(title);

        // Add some body text
        auto label = CCLabelBMFont::create("Popup is working, let's gooo!", "chatFont.fnt");
        label->setPosition({winSize.width / 2, winSize.height / 2});
        label->scaleToWidth(220.f);
        m_mainLayer->addChild(label);

        return true;
    }
};

// 2. Hook MenuLayer to trigger the popup on startup
class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }

        // Trigger the popup to show up when the menu loads
        // (Note: This will pop up every time you return to the main menu for easy testing)
        FrameCounterPopup::create()->show();

        return true;
    }
};