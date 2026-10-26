// Frame Window & CBF Precision Counter
// Target: Geometry Dash 2.2081 | Geode SDK v5.11.2
//
// What it does
//  - Tracks the physics grid (default 240 TPS) in PlayLayer::update.
//  - On every input (GJBaseGameLayer::handleButton) it measures where the input
//    landed inside the current physics tick and shows that offset on a HUD.
//  - Plays resources/ding.ogg through FMOD with a pitch derived from that offset.
//
// Definitions
//  phase  = fract(simTime / tickLen)           0..1, position inside the current tick
//  delay  = (phase == 0) ? 0 : 1 - phase       0..1 ticks until the input is consumed
//           0 -> input sits right on a tick (tight window), ->1 -> just missed a tick
//  pitch  = 2.5 * 0.2^(delay^sharpness)        clamped to [0.5, 2.5]
//
// Notes
//  - Inputs are timestamped with a steady clock, anchored to the end of the last
//    PlayLayer::update. With vanilla input handling this reflects frame-quantised
//    input; with Click Between Frames the calls arrive mid-frame, so you see the
//    real sub-frame phase.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace fw {
    using Clock = std::chrono::steady_clock;

    constexpr float kPitchMin = 0.5f;
    constexpr float kPitchMax = 2.5f;

    inline double nowSeconds() {
        return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    }

    inline float mapDelayToPitch(float delay) {
        float sharp = static_cast<float>(Mod::get()->getSettingValue<double>("pitch-sharpness"));
        sharp = std::clamp(sharp, 0.2f, 4.0f);
        float t = std::pow(std::clamp(delay, 0.f, 1.f), sharp);
        // exponential interpolation: 2.5 at t=0 -> 0.5 at t=1 (~1.1 at t=0.5)
        float pitch = kPitchMax * std::pow(kPitchMin / kPitchMax, t);
        return std::clamp(pitch, kPitchMin, kPitchMax);
    }

    // -----------------------------------------------------------------------
    // FMOD ding player (lazy loaded, survives engine restarts via re-check)
    // -----------------------------------------------------------------------
    class DingPlayer {
    public:
        static DingPlayer& get() {
            static DingPlayer inst;
            return inst;
        }

        void play(float pitch) {
            auto* engine = FMODAudioEngine::sharedEngine();
            if (!engine || !engine->m_system) return;
            FMOD::System* system = engine->m_system;

            if (!ensureSound(system)) return;

            double nowT = nowSeconds();
            if (nowT - m_lastPlay < 0.012) return; // avoid voice spam on very rapid events
            m_lastPlay = nowT;

            FMOD::Channel* channel = nullptr;
            // start paused so pitch/volume are applied before the first audible sample
            if (system->playSound(m_sound, nullptr, true, &channel) != FMOD_OK || !channel) return;

            float vol = static_cast<float>(Mod::get()->getSettingValue<double>("volume"));
            channel->setVolume(std::clamp(vol, 0.f, 2.f));
            channel->setPitch(std::clamp(pitch, kPitchMin, kPitchMax));
            channel->setPaused(false);
        }

    private:
        bool ensureSound(FMOD::System* system) {
            if (m_sound && m_system == system) return true;
            m_sound = nullptr;
            m_system = system;

            auto path = Mod::get()->getResourcesDir() / "ding.ogg";
            auto pathStr = geode::utils::string::pathToString(path);

            FMOD::Sound* snd = nullptr;
            FMOD_RESULT res = system->createSound(pathStr.c_str(), FMOD_DEFAULT | FMOD_CREATESAMPLE, nullptr, &snd);
            if (res != FMOD_OK || !snd) {
                if (!m_warned) {
                    log::error("Failed to load ding.ogg from '{}' (FMOD result {})", pathStr, static_cast<int>(res));
                    m_warned = true;
                }
                return false;
            }
            m_sound = snd;
            m_warned = false;
            return true;
        }

        FMOD::System* m_system = nullptr;
        FMOD::Sound* m_sound = nullptr;
        double m_lastPlay = 0.0;
        bool m_warned = false;
    };
}

// ---------------------------------------------------------------------------
// HUD overlay node
// ---------------------------------------------------------------------------
class FrameWindowOverlay : public CCNode {
protected:
    CCLabelBMFont* m_mainLabel = nullptr;
    CCLabelBMFont* m_subLabel = nullptr;
    CCLabelBMFont* m_phaseLabel = nullptr;
    CCLayerColor* m_barBg = nullptr;
    CCLayerColor* m_barMarker = nullptr;
    CCLayerColor* m_phaseMarker = nullptr;

    static constexpr float kBarW = 120.f;
    static constexpr float kBarH = 5.f;

    bool init() override {
        if (!CCNode::init()) return false;

        this->setID("frame-window-overlay"_spr);
        this->setAnchorPoint({0.f, 1.f});
        this->setContentSize({kBarW, 40.f});

        m_mainLabel = CCLabelBMFont::create("Window: --", "bigFont.fnt");
        m_mainLabel->setAnchorPoint({0.f, 1.f});
        m_mainLabel->setScale(0.45f);
        m_mainLabel->setPosition({0.f, 40.f});
        m_mainLabel->setID("main-label");
        this->addChild(m_mainLabel);

        m_subLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_subLabel->setAnchorPoint({0.f, 1.f});
        m_subLabel->setScale(0.5f);
        m_subLabel->setPosition({0.f, 24.f});
        m_subLabel->setID("sub-label");
        this->addChild(m_subLabel);

        m_phaseLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_phaseLabel->setAnchorPoint({0.f, 1.f});
        m_phaseLabel->setScale(0.4f);
        m_phaseLabel->setOpacity(150);
        m_phaseLabel->setPosition({0.f, 14.f});
        m_phaseLabel->setID("phase-label");
        this->addChild(m_phaseLabel);

        m_barBg = CCLayerColor::create({0, 0, 0, 120}, kBarW, kBarH);
        m_barBg->setPosition({0.f, 0.f});
        m_barBg->setID("bar-bg");
        this->addChild(m_barBg);

        m_phaseMarker = CCLayerColor::create({255, 255, 255, 90}, 1.5f, kBarH + 2.f);
        m_phaseMarker->setPosition({0.f, -1.f});
        m_phaseMarker->setID("phase-marker");
        this->addChild(m_phaseMarker, 1);

        m_barMarker = CCLayerColor::create({90, 255, 120, 255}, 3.f, kBarH + 4.f);
        m_barMarker->setPosition({0.f, -2.f});
        m_barMarker->setID("input-marker");
        this->addChild(m_barMarker, 2);

        return true;
    }

public:
    static FrameWindowOverlay* create() {
        auto ret = new FrameWindowOverlay();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void applyLayout() {
        float scale = static_cast<float>(Mod::get()->getSettingValue<double>("hud-scale"));
        this->setScale(std::clamp(scale, 0.3f, 3.f));
        bool showBar = Mod::get()->getSettingValue<bool>("show-bar");
        m_barBg->setVisible(showBar);
        m_barMarker->setVisible(showBar);
        m_phaseMarker->setVisible(showBar);
    }

    // delay in ticks [0..1]
    void showInput(float delay, double tickLen, float pitch, int count, double avgDelay, double bestDelay) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "Window: +%.3f tk", delay);
        m_mainLabel->setString(buf);

        // colour: green (tight) -> yellow -> red (late)
        GLubyte r = static_cast<GLubyte>(std::clamp(delay * 2.f, 0.f, 1.f) * 255.f);
        GLubyte g = static_cast<GLubyte>(std::clamp((1.f - delay) * 2.f, 0.f, 1.f) * 255.f);
        m_mainLabel->setColor({r, g, 80});

        std::snprintf(buf, sizeof(buf), "%.2f ms | x%.2f | avg %.3f best %.3f | n=%d",
            delay * tickLen * 1000.0, pitch, avgDelay, bestDelay, count);
        m_subLabel->setString(buf);

        m_barMarker->setPositionX(std::clamp(delay, 0.f, 1.f) * (kBarW - 3.f));
        m_barMarker->setColor({r, g, 80});

        // flash
        m_mainLabel->stopAllActions();
        m_mainLabel->setOpacity(255);
        m_mainLabel->runAction(CCSequence::create(
            CCDelayTime::create(1.2f),
            CCFadeTo::create(0.6f, 120),
            nullptr
        ));
    }

    void showPhase(float phase) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "phase %.3f", phase);
        m_phaseLabel->setString(buf);
        m_phaseMarker->setPositionX(std::clamp(phase, 0.f, 1.f) * (kBarW - 1.5f));
    }

    void clearInput() {
        m_mainLabel->setString("Window: --");
        m_mainLabel->setColor({255, 255, 255});
        m_subLabel->setString("");
    }
};

// ---------------------------------------------------------------------------
// PlayLayer hook: timing grid, overlay, ding-on-update
// ---------------------------------------------------------------------------
class $modify(FWPlayLayer, PlayLayer) {
    struct Fields {
        Ref<FrameWindowOverlay> overlay;

        double simTime = 0.0;         // accumulated physics time (s), reset each attempt
        double lastFrameEndWall = 0.0; // wall-clock at end of last update
        float lastTimeWarp = 1.f;
        bool haveFrame = false;

        int inputCount = 0;
        double delaySum = 0.0;
        double bestDelay = 1.0;
        double lastUpdateDing = 0.0;
    };

    static double tickLength() {
        int tps = static_cast<int>(Mod::get()->getSettingValue<int64_t>("tps"));
        tps = std::clamp(tps, 1, 100000);
        return 1.0 / static_cast<double>(tps);
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        return true;
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        this->buildOverlay();
    }

    void buildOverlay() {
        auto f = m_fields.self();
        if (f->overlay || !m_uiLayer) return;
        auto overlay = FrameWindowOverlay::create();
        if (!overlay) return;

        auto win = CCDirector::get()->getWinSize();
        overlay->setPosition({8.f, win.height - 40.f});
        overlay->applyLayout();
        overlay->setVisible(Mod::get()->getSettingValue<bool>("enabled"));
        m_uiLayer->addChild(overlay, 9999);
        f->overlay = overlay;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        auto f = m_fields.self();
        f->simTime = 0.0;
        f->haveFrame = false;
        if (f->overlay) f->overlay->clearInput();
    }

    void update(float dt) {
        PlayLayer::update(dt);

        auto f = m_fields.self();
        float warp = m_gameState.m_timeWarp;
        if (!(warp > 0.f)) warp = 1.f;

        f->simTime += static_cast<double>(dt) * static_cast<double>(warp);
        f->lastTimeWarp = warp;
        f->lastFrameEndWall = fw::nowSeconds();
        f->haveFrame = true;

        if (!f->overlay || !Mod::get()->getSettingValue<bool>("enabled")) return;

        double tick = tickLength();
        double phase = std::fmod(f->simTime, tick) / tick;
        f->overlay->showPhase(static_cast<float>(phase));

        // optional: ding on every frame-window counter refresh (throttled)
        if (Mod::get()->getSettingValue<bool>("ding-on-update")) {
            double now = f->lastFrameEndWall;
            if (now - f->lastUpdateDing >= 0.1) {
                f->lastUpdateDing = now;
                float delay = (phase <= 1e-6) ? 0.f : static_cast<float>(1.0 - phase);
                fw::DingPlayer::get().play(fw::mapDelayToPitch(delay));
            }
        }
    }

    // called from the GJBaseGameLayer::handleButton hook below
    void registerInput(bool down, int button, bool isPlayer1) {
        (void)button; (void)isPlayer1;
        if (!Mod::get()->getSettingValue<bool>("enabled")) return;
        if (!down && !Mod::get()->getSettingValue<bool>("count-releases")) return;

        auto f = m_fields.self();
        if (!f->haveFrame) return;
        if (m_player1 && m_player1->m_isDead) return;

        double now = fw::nowSeconds();
        double since = now - f->lastFrameEndWall;
        if (since < 0.0 || since > 0.1) return; // stale (pause/resume/lag spike) -> ignore sample

        double tick = tickLength();
        double simAtInput = f->simTime + since * static_cast<double>(f->lastTimeWarp);
        double phase = std::fmod(simAtInput, tick) / tick;     // 0..1
        float delay = (phase <= 1e-6) ? 0.f : static_cast<float>(1.0 - phase);
        float pitch = fw::mapDelayToPitch(delay);

        f->inputCount++;
        f->delaySum += delay;
        f->bestDelay = std::min(f->bestDelay, static_cast<double>(delay));

        if (f->overlay) {
            f->overlay->applyLayout();
            f->overlay->showInput(delay, tick, pitch, f->inputCount,
                f->delaySum / f->inputCount, f->bestDelay);
        }

        if (Mod::get()->getSettingValue<bool>("ding-on-input")) {
            fw::DingPlayer::get().play(pitch);
        }
    }

    void onQuit() {
        auto f = m_fields.self();
        if (f->overlay) {
            f->overlay->removeFromParent();
            f->overlay = nullptr;
        }
        PlayLayer::onQuit();
    }
};

// ---------------------------------------------------------------------------
// Input hook (covers pushButton/releaseButton for both players)
// ---------------------------------------------------------------------------
class $modify(FWBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        // Register before the original so the sample reflects the arrival time
        if (auto pl = PlayLayer::get()) {
            if (static_cast<GJBaseGameLayer*>(pl) == this) {
                static_cast<FWPlayLayer*>(pl)->registerInput(down, button, isPlayer1);
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};