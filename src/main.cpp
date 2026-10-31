#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>

using namespace geode::prelude;

// IPGDGLL Supabase project. The publishable key is meant to be public (it's in the site's JS too);
// what it can do is limited by Row Level Security on the database.
static constexpr auto IPGDGLL_URL = "https://daqvzyguqzfennpodsdh.supabase.co";
static constexpr auto IPGDGLL_KEY = "sb_publishable_ZroKxa7t7nlpq-AvwZMVPg_3RHOBVrc";

// Formats seconds as m:ss or h:mm:ss
static std::string formatTime(double seconds) {
    int total = static_cast<int>(seconds);
    int h = total / 3600;
    int m = (total / 60) % 60;
    int s = total % 60;
    if (h > 0) return fmt::format("{}:{:02}:{:02}", h, m, s);
    return fmt::format("{}:{:02}", m, s);
}

static web::WebRequest supabaseRequest() {
    web::WebRequest req;
    req.header("apikey", IPGDGLL_KEY);
    req.header("Content-Type", "application/json");
    req.timeout(std::chrono::seconds(10));
    return req;
}

// Sends progress to IPGDGLL as a pending record. Runs in the background and survives leaving the level.
static void submitToIpgdgll(int levelID, int percent, int attempts) {
    auto token = Mod::get()->getSettingValue<std::string>("ipgdgll-token");
    if (token.empty()) return;

    auto body = matjson::makeObject({
        { "p_token", token },
        { "p_gd_id", std::to_string(levelID) },
        { "p_progress", percent },
        { "p_attempts", attempts },
    });

    auto req = supabaseRequest();
    req.bodyJSON(body);

    async::spawn(
        req.post(fmt::format("{}/rest/v1/rpc/mod_submit_record", IPGDGLL_URL)),
        [levelID, percent](web::WebResponse res) {
            if (!res.ok()) {
                log::warn("IPGDGLL submit failed: HTTP {} {}", res.code(), res.string().unwrapOr(""));
                Notification::create("IPGDGLL: couldn't send progress", NotificationIcon::Error)->show();
                return;
            }
            auto status = res.json().unwrapOr(matjson::Value()).asString().unwrapOr("");
            auto key = fmt::format("ipgdgll-sent-{}", levelID);

            if (status == "submitted" || status == "updated") {
                Mod::get()->setSavedValue<int>(key, percent);
                Notification::create(
                    fmt::format("IPGDGLL: {}% sent for review", percent), NotificationIcon::Success
                )->show();
            } else if (status == "not_higher") {
                Mod::get()->setSavedValue<int>(key, percent);
            } else if (status == "bad_token") {
                Notification::create("IPGDGLL: mod token is wrong, copy it again from your profile", NotificationIcon::Error, 4.f)->show();
            } else if (status == "banned") {
                Notification::create("IPGDGLL: your account is banned from submitting", NotificationIcon::Error)->show();
            } else {
                log::info("IPGDGLL submit result: {}", status);
            }
        }
    );
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

        // IPGDGLL
        bool onList = false;
        std::string listLine;
        async::TaskHolder<web::WebResponse> rankTask;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        this->fetchListInfo();

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

    int levelID() {
        return m_level ? m_level->m_levelID.value() : 0;
    }

    // Looks this level up on IPGDGLL by its GD level ID
    void fetchListInfo() {
        int id = this->levelID();
        if (id <= 0) return; // local / editor levels aren't on the list

        auto url = fmt::format(
            "{}/rest/v1/levels?select=name,position,possible&gd_id=eq.{}&limit=1", IPGDGLL_URL, id
        );

        m_fields->rankTask.spawn(supabaseRequest().get(url), [this](web::WebResponse res) {
            if (!res.ok()) {
                log::warn("IPGDGLL lookup failed: HTTP {}", res.code());
                return;
            }
            auto json = res.json().unwrapOr(matjson::Value());
            if (!json.isArray() || json.size() == 0) return; // not on the list

            auto entry = json[0];
            int pos = entry["position"].asInt().unwrapOr(0);
            bool possible = entry["possible"].asBool().unwrapOr(true); // null counts as possible

            auto f = m_fields.self();
            f->onList = true;
            f->listLine = pos > 0
                ? fmt::format("IPGDGLL #{} ({})", pos, possible ? "Possible" : "Impossible")
                : "On IPGDGLL";
            this->refreshStats();
        });
    }

    bool canSubmit() {
        auto mod = Mod::get();
        return m_fields->onList
            && mod->getSettingValue<bool>("ipgdgll-submit")
            && !mod->getSettingValue<std::string>("ipgdgll-token").empty()
            && !m_isPracticeMode
            && !m_isPlatformer
            && !m_isTestMode
            && !m_startPosObject; // start position runs aren't real records
    }

    void maybeSubmit(int percent) {
        if (!this->canSubmit()) return;
        int id = this->levelID();
        int alreadySent = Mod::get()->getSavedValue<int>(fmt::format("ipgdgll-sent-{}", id), 0);
        if (percent <= alreadySent) return;
        submitToIpgdgll(id, percent, m_attempts);
    }

    void refreshStats() {
        auto f = m_fields.self();
        if (!f->label) return;

        int totalDeaths = Mod::get()->getSavedValue<int>("total-deaths", 0);

        std::string text;
        if (f->onList && Mod::get()->getSettingValue<bool>("ipgdgll-rank")) {
            text += f->listLine + "\n";
        }
        text += fmt::format("Attempt {}\nDeaths: {}\n", m_attempts, f->deaths);
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
                this->maybeSubmit(percent);
            }
        }

        this->refreshStats();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto f = m_fields.self();
        if (!m_isPaused && !f->completed) f->playTime += dt;

        // Detect level completion without needing an extra hook
        if (m_hasCompletedLevel && !f->completed) {
            f->completed = true;
            if (!m_isPracticeMode && !m_isPlatformer) {
                f->bestPercent = 100;
                this->maybeSubmit(100);
            }
            this->showPopup(fmt::format("GG! {} attempts", m_attempts));
            this->refreshStats();
        }

        if (!f->label) return;

        // Only rebuild the text when the shown second changes
        int second = static_cast<int>(f->playTime);
        if (second != f->lastShownSecond) {
            f->lastShownSecond = second;
            this->refreshStats();
        }
    }
};
