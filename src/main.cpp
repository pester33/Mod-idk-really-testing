#include <Geode/Geode.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>

using namespace geode::prelude;

// IPGDGLL Supabase project. The publishable key is meant to be public (it's in the site's JS too);
// what it can do is limited by Row Level Security on the database.
static constexpr auto IPGDGLL_URL = "https://daqvzyguqzfennpodsdh.supabase.co";
static constexpr auto IPGDGLL_KEY = "sb_publishable_ZroKxa7t7nlpq-AvwZMVPg_3RHOBVrc";

static web::WebRequest supabaseRequest() {
    web::WebRequest req;
    req.header("apikey", IPGDGLL_KEY);
    req.header("Content-Type", "application/json");
    req.timeout(std::chrono::seconds(10));
    return req;
}

// ---------------------------------------------------------------------------
// The list popup
// ---------------------------------------------------------------------------

struct ListLevel {
    int rank = 0;
    std::string name;
    std::string creator;
    std::string gdID;
    bool possible = true;
};

class IpgdgllListPopup : public Popup {
protected:
    static constexpr float WIDTH = 400.f;
    static constexpr float HEIGHT = 270.f;
    static constexpr float LIST_W = 360.f;
    static constexpr float LIST_H = 195.f;
    static constexpr float ROW_H = 34.f;

    ScrollLayer* m_list = nullptr;
    CCLabelBMFont* m_status = nullptr;
    std::vector<ListLevel> m_levels;
    std::string m_filter = "All";
    std::vector<CCMenuItemSpriteExtra*> m_tabs;
    async::TaskHolder<web::WebResponse> m_task;

    bool init() {
        if (!Popup::init(WIDTH, HEIGHT)) return false;
        this->setTitle("IPGDGLL List");

        auto center = m_mainLayer->getContentSize() / 2;

        // Dark background behind the list
        auto bg = CCScale9Sprite::create("square02b_001.png");
        bg->setColor({ 0, 0, 0 });
        bg->setOpacity(80);
        bg->setContentSize({ LIST_W + 6.f, LIST_H + 6.f });
        bg->setPosition({ center.width, center.height - 16.f });
        m_mainLayer->addChild(bg);

        m_list = ScrollLayer::create({ LIST_W, LIST_H });
        m_list->setPosition({ center.width - LIST_W / 2, center.height - 16.f - LIST_H / 2 });
        m_list->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(3.f));
        m_mainLayer->addChild(m_list);

        m_status = CCLabelBMFont::create("Loading...", "bigFont.fnt");
        m_status->setScale(0.5f);
        m_status->setPosition({ center.width, center.height - 16.f });
        m_mainLayer->addChild(m_status, 5);

        // All / Possible / Impossible tabs (same as the website)
        float tabY = HEIGHT - 46.f;
        float tabX = center.width - 110.f;
        for (auto name : { "All", "Possible", "Impossible" }) {
            auto spr = ButtonSprite::create(name, "bigFont.fnt", "GJ_button_04.png", 0.8f);
            spr->setScale(0.5f);
            auto btn = CCMenuItemExt::createSpriteExtra(spr, [this, name](CCMenuItemSpriteExtra*) {
                m_filter = name;
                this->rebuild();
            });
            btn->setUserObject(CCString::create(name));
            btn->setPosition({ tabX, tabY });
            m_buttonMenu->addChild(btn);
            m_tabs.push_back(btn);
            tabX += 110.f;
        }
        this->updateTabs();

        // Open the website
        auto webSpr = ButtonSprite::create("Website", "goldFont.fnt", "GJ_button_01.png", 0.8f);
        webSpr->setScale(0.55f);
        auto webBtn = CCMenuItemExt::createSpriteExtra(webSpr, [](CCMenuItemSpriteExtra*) {
            web::openLinkInBrowser("https://ipgdgll.vercel.app");
        });
        webBtn->setPosition({ center.width, 14.f });
        m_buttonMenu->addChild(webBtn);

        this->load();
        return true;
    }

    void updateTabs() {
        for (auto btn : m_tabs) {
            auto name = static_cast<CCString*>(btn->getUserObject())->getCString();
            auto spr = static_cast<ButtonSprite*>(btn->getNormalImage());
            spr->updateBGImage(m_filter == name ? "GJ_button_02.png" : "GJ_button_04.png");
        }
    }

    void load() {
        auto url = fmt::format(
            "{}/rest/v1/levels?select=name,creator,position,possible,gd_id&order=position", IPGDGLL_URL
        );
        m_task.spawn(supabaseRequest().get(url), [this](web::WebResponse res) {
            if (!res.ok()) {
                log::warn("IPGDGLL list failed: HTTP {} {}", res.code(), res.string().unwrapOr(""));
                m_status->setString("Couldn't load the list.\nCheck your internet.");
                return;
            }
            auto json = res.json().unwrapOr(matjson::Value());
            if (!json.isArray()) {
                m_status->setString("Couldn't read the list.");
                return;
            }

            m_levels.clear();
            int rank = 1;
            for (auto& entry : json) {
                ListLevel lvl;
                lvl.rank = rank++; // same numbering as the website
                lvl.name = entry["name"].asString().unwrapOr("Unknown");
                lvl.creator = entry["creator"].asString().unwrapOr("");
                lvl.possible = entry["possible"].asBool().unwrapOr(true); // null counts as possible
                auto& gd = entry["gd_id"];
                if (gd.isString()) lvl.gdID = gd.asString().unwrapOr("");
                else if (gd.isNumber()) lvl.gdID = std::to_string(gd.asInt().unwrapOr(0));
                if (lvl.gdID == "0") lvl.gdID.clear();
                m_levels.push_back(std::move(lvl));
            }
            this->rebuild();
        });
    }

    void rebuild() {
        this->updateTabs();
        auto content = m_list->m_contentLayer;
        content->removeAllChildren();

        int shown = 0;
        for (auto& lvl : m_levels) {
            if (m_filter == "Possible" && !lvl.possible) continue;
            if (m_filter == "Impossible" && lvl.possible) continue;
            content->addChild(this->createRow(lvl));
            shown++;
        }

        if (m_levels.empty()) m_status->setString("The list is empty.");
        else if (shown == 0) m_status->setString(fmt::format("No {} levels.", m_filter).c_str());
        m_status->setVisible(shown == 0);

        content->updateLayout();
        m_list->scrollToTop();
    }

    CCNode* createRow(ListLevel const& lvl) {
        auto row = CCNode::create();
        row->setContentSize({ LIST_W, ROW_H });

        auto rowBg = CCScale9Sprite::create("square02b_001.png");
        rowBg->setColor(lvl.rank % 2 ? ccColor3B{ 161, 88, 44 } : ccColor3B{ 194, 114, 62 });
        rowBg->setContentSize({ LIST_W - 4.f, ROW_H });
        rowBg->setPosition({ LIST_W / 2, ROW_H / 2 });
        row->addChild(rowBg);

        auto rankLbl = CCLabelBMFont::create(fmt::format("#{}", lvl.rank).c_str(), "goldFont.fnt");
        rankLbl->limitLabelWidth(40.f, 0.65f, 0.3f);
        rankLbl->setPosition({ 26.f, ROW_H / 2 });
        row->addChild(rankLbl);

        auto nameLbl = CCLabelBMFont::create(lvl.name.c_str(), "bigFont.fnt");
        nameLbl->setAnchorPoint({ 0.f, 0.5f });
        nameLbl->limitLabelWidth(190.f, 0.5f, 0.2f);
        nameLbl->setPosition({ 52.f, ROW_H / 2 + 6.f });
        row->addChild(nameLbl);

        auto info = lvl.creator.empty() ? std::string() : "by " + lvl.creator + "  ";
        info += lvl.possible ? "Possible" : "Impossible";
        auto infoLbl = CCLabelBMFont::create(info.c_str(), "chatFont.fnt");
        infoLbl->setAnchorPoint({ 0.f, 0.5f });
        infoLbl->limitLabelWidth(190.f, 0.55f, 0.2f);
        infoLbl->setColor(lvl.possible ? ccColor3B{ 170, 255, 170 } : ccColor3B{ 255, 150, 150 });
        infoLbl->setPosition({ 52.f, ROW_H / 2 - 7.f });
        row->addChild(infoLbl);

        auto menu = CCMenu::create();
        menu->setPosition({ 0.f, 0.f });
        menu->setContentSize(row->getContentSize());
        row->addChild(menu);

        if (!lvl.gdID.empty()) {
            auto spr = ButtonSprite::create("View", "bigFont.fnt", "GJ_button_01.png", 0.8f);
            spr->setScale(0.5f);
            auto id = lvl.gdID;
            auto btn = CCMenuItemExt::createSpriteExtra(spr, [id](CCMenuItemSpriteExtra*) {
                // Search GD for the level ID; this opens the level in the normal level browser
                auto search = GJSearchObject::create(SearchType::Search, id);
                auto scene = LevelBrowserLayer::scene(search);
                CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(0.5f, scene));
            });
            btn->setPosition({ LIST_W - 38.f, ROW_H / 2 });
            menu->addChild(btn);
        } else {
            auto noId = CCLabelBMFont::create("No ID", "goldFont.fnt");
            noId->setScale(0.4f);
            noId->setOpacity(150);
            noId->setPosition({ LIST_W - 38.f, ROW_H / 2 });
            row->addChild(noId);
        }

        return row;
    }

public:
    static IpgdgllListPopup* create() {
        auto ret = new IpgdgllListPopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// "IPGDGLL List" button on the Search screen
// ---------------------------------------------------------------------------

class $modify(IpgdgllSearchLayer, LevelSearchLayer) {
    bool init(int type) {
        if (!LevelSearchLayer::init(type)) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto spr = ButtonSprite::create("IPGDGLL List", "goldFont.fnt", "GJ_button_01.png", 0.8f);
        spr->setScale(0.6f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) {
            if (auto popup = IpgdgllListPopup::create()) popup->show();
        });
        btn->setID("ipgdgll-list-button"_spr);

        auto menu = CCMenu::create();
        menu->setID("ipgdgll-menu"_spr);
        menu->setPosition({ 0.f, 0.f });
        menu->addChild(btn);
        // Left side of the screen, below the back button
        btn->setPosition({ 52.f, winSize.height / 2.f - 20.f });
        this->addChild(menu, 10);

        return true;
    }
};

// ---------------------------------------------------------------------------
// Optional: send new bests on IPGDGLL levels as pending records (needs mod token)
// ---------------------------------------------------------------------------

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
                // "not_listed" etc: level isn't on the list, nothing to do
                log::info("IPGDGLL submit result: {}", status);
            }
        }
    );
}

class $modify(IpgdgllPlayLayer, PlayLayer) {
    struct Fields {
        int best = 0;
        bool diedThisAttempt = false;
        bool completed = false;
        bool onList = false;
        async::TaskHolder<web::WebResponse> lookup;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        // Only check the list if the player set up submitting
        int id = m_level ? m_level->m_levelID.value() : 0;
        if (id > 0 && Mod::get()->getSettingValue<bool>("ipgdgll-submit")
            && !Mod::get()->getSettingValue<std::string>("ipgdgll-token").empty()) {
            auto url = fmt::format("{}/rest/v1/levels?select=id&gd_id=eq.{}&limit=1", IPGDGLL_URL, id);
            m_fields->lookup.spawn(supabaseRequest().get(url), [this](web::WebResponse res) {
                if (!res.ok()) return;
                auto json = res.json().unwrapOr(matjson::Value());
                m_fields->onList = json.isArray() && json.size() > 0;
            });
        }
        return true;
    }

    bool canSubmit() {
        return m_fields->onList
            && Mod::get()->getSettingValue<bool>("ipgdgll-submit")
            && !Mod::get()->getSettingValue<std::string>("ipgdgll-token").empty()
            && !m_isPracticeMode
            && !m_isPlatformer
            && !m_isTestMode
            && !m_startPosObject; // start position runs aren't real records
    }

    void maybeSubmit(int percent) {
        if (!this->canSubmit()) return;
        int id = m_level->m_levelID.value();
        int alreadySent = Mod::get()->getSavedValue<int>(fmt::format("ipgdgll-sent-{}", id), 0);
        if (percent <= alreadySent) return;
        submitToIpgdgll(id, percent, m_attempts);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        m_fields->diedThisAttempt = false;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        if (object == m_anticheatSpike) return;

        auto f = m_fields.self();
        if (f->diedThisAttempt || f->completed || !player || !player->m_isDead) return;
        f->diedThisAttempt = true;

        if (m_isPlatformer || m_isPracticeMode) return;
        int percent = this->getCurrentPercentInt();
        if (percent > f->best) {
            f->best = percent;
            this->maybeSubmit(percent);
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto f = m_fields.self();
        if (m_hasCompletedLevel && !f->completed) {
            f->completed = true;
            this->maybeSubmit(100);
        }
    }
};
