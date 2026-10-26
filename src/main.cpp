#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>

using namespace geode::prelude;

class $modify(MyMenuLayer, MenuLayer) {
    // We use a static boolean so the popup only shows ONCE when you first open the game, 
    // not every single time you go back to the main menu from a level.
    static inline bool hasShownPopup = false;

    bool init() {
        if (!MenuLayer::init()) return false;

        if (!hasShownPopup) {
            hasShownPopup = true;

            // Trigger the classic GD popup box
            FLAlertLayer::create(
                "PRIVATE MOD",                                     // Title of the popup
                "Hey!, this is a private mod, so if you get "      // Body text line 1
                "sent this mod, you are basically on the "         // Body text line 2
                "very rare side:)",                                // Body text line 3
                "OK"                                               // Button text
            )->show();
        }

        return true;
    }
};