#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// Formats seconds as m:ss or h:mm:ss
static std::string formatTime(double seconds) {
    int total = static_cast<int>(seconds);
    int h = total / 3600;
    int m = (total / 60) % 60;
    int s = total % 60;
    if (h > 0) return fmt::format("{}:{:02}:{:02}", h, m, s);
    return fmt::format("{}:{:02}", m, s);
}

class $modify(SessionStatsLayer, PlayLayer) {
    struct Fields {
        CCLabelBMFont* label = nullptr;
        CCLabelBMFont* popup = nullptr;
        int deaths = 0;
        int bestPercent = 0;
        int lastDeathPercent = -1;
        double playTime = 0.0;
        int lastShownSecond = -1;
        bool diedThisAttempt = false;
        bool completed = false;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("show-overlay")) return true;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto position = mod->getSettingValue<std::string>("position");
        float scale = static_cast<float>(mod->getSettingValue<double>("scale"));
        auto opacity = static_cast<GLubyte>(mod->getSettingValue<int64_t>("opacity"));

        bool top = position.starts_with("Top");
        bool left = position.ends_with("Left");
        float pad = 5.f;

        auto label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setAlignment(left ? kCCTextAlignmentLeft : kCCTextAlignmentRight);
        label->setAnchorPoint({ left ? 0.f : 1.f, top ? 1.f : 0.f });
        label->setPosition({
            left ? pad : winSize.width - pad,
            top ? winSize.height - pad : pad
        });
        label->setScale(scale);
        label->setOpacity(opacity);
        label->setID("session-stats-label"_spr);
        this->addChild(label, 9999);
        m_fields->label = label;

        auto popup = CCLabelBMFont::create("NEW BEST!", "goldFont.fnt");
        popup->setPosition({ winSize.width / 2.f, winSize.height * 0.72f });
        popup->setScale(0.9f);
        popup->setOpacity(0);
        popup->setID("session-stats-popup"_spr);
        this->addChild(popup, 9999);
        m_fields->popup = popup;

        this->refreshStats();
        return true;
    }

    void refreshStats() {
        auto f = m_fields.self();
        if (!f->label) return;

        int totalDeaths = Mod::get()->getSavedValue<int>("total-deaths", 0);

        std::string text = fmt::format("Attempt {}\nDeaths: {}\n", m_attempts, f->deaths);
        if (!m_isPlatformer) {
            text += fmt::format("Session Best: {}%\n", f->bestPercent);
            if (f->lastDeathPercent >= 0) {
                text += fmt::format("Last Death: {}%\n", f->lastDeathPercent);
            }
        }
        text += fmt::format("Time: {}\n", formatTime(f->playTime));
        text += fmt::format("All-time Deaths: {}", totalDeaths);
        if (m_isPracticeMode) text += "\n(Practice)";

        f->label->setString(text.c_str());
    }

    void showPopup(std::string const& text) {
        auto popup = m_fields->popup;
        if (!popup || !Mod::get()->getSettingValue<bool>("best-popup")) return;
        popup->setString(text.c_str());
        popup->stopAllActions();
        popup->setOpacity(0);
        popup->setScale(1.2f);
        popup->runAction(CCSequence::create(
            CCSpawn::create(CCFadeIn::create(0.12f), CCEaseBackOut::create(CCScaleTo::create(0.25f, 0.9f)), nullptr),
            CCDelayTime::create(0.9f),
            CCFadeOut::create(0.4f),
            nullptr
        ));
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        m_fields->diedThisAttempt = false;
        this->refreshStats();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);

        // GD "kills" the player with a hidden spike on load as an anti-cheat check; ignore it.
        if (object == m_anticheatSpike) return;

        auto f = m_fields.self();
        // Only count once per attempt (dual mode can call this twice) and only real deaths.
        if (f->diedThisAttempt || f->completed || !player || !player->m_isDead) return;
        f->diedThisAttempt = true;
        f->deaths++;

        auto mod = Mod::get();
        mod->setSavedValue<int>("total-deaths", mod->getSavedValue<int>("total-deaths", 0) + 1);

        if (!m_isPlatformer) {
            int percent = this->getCurrentPercentInt();
            f->lastDeathPercent = percent;
            if (!m_isPracticeMode && percent > f->bestPercent) {
                bool hadBest = f->bestPercent > 0;
                f->bestPercent = percent;
                if (hadBest) this->showPopup(fmt::format("NEW BEST! {}%", percent));
            }
        }

        this->refreshStats();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto f = m_fields.self();
        if (!f->label) return;

        if (!m_isPaused && !f->completed) f->playTime += dt;

        // Detect level completion without needing an extra hook
        if (m_hasCompletedLevel && !f->completed) {
            f->completed = true;
            if (!m_isPracticeMode && !m_isPlatformer) f->bestPercent = 100;
            this->showPopup(fmt::format("GG! {} attempts", m_attempts));
            this->refreshStats();
        }

        // Only rebuild the text when the shown second changes
        int second = static_cast<int>(f->playTime);
        if (second != f->lastShownSecond) {
            f->lastShownSecond = second;
            this->refreshStats();
        }
    }
};
