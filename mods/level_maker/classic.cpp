// SPDX-License-Identifier: GPL-3.0-or-later
// level-maker: build, play and share levels of your own (Classic).
//
// A button of the main menu opens it. Levels are built in the game's own scene
// (lua_editor.hpp), played as a hidden episode (lua_core.hpp) and saved in
// settings.lua next to the game's data, which the game leaves alone. A level is
// shared as a code (codec.hpp) or its QR code, once it was beaten.
//
// The host part answers the script (send / read, common/script_link.hpp): codes,
// QR codes, the clipboard, file dialogs, the camera that reads QR codes (its
// picture drawn over the game), and in the editor the keys, the right
// mouse button and the wheel.
#include <SDL3/SDL.h>

#include <algorithm>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "common/game_scripts.hpp"
#include "common/script_link.hpp"
#include "common/statistics.hpp"
#include "level_maker/camera.hpp"
#include "level_maker/codec.hpp"
#include "level_maker/lua_core.hpp"
#include "level_maker/lua_editor.hpp"
#include "level_maker/lua_hub.hpp"
#include "level_maker/lua_texts.hpp"
#include "level_maker/lua_ui.hpp"
#include "level_maker/qr.hpp"
#include "mod.hpp"
#include "recomp/vfs.hpp"

namespace {

using slingshot::ModContext;
namespace lm = slingshot::level_maker;

constexpr const char* kMessages = "cache/slingshot-level-maker/";
constexpr const char* kReply = "cache/slingshot-level-maker-reply";

// The main menu's card, next to Episodes and Mighty League (the art of the
// Daily Challenge card, which hide-dead-services hides).
constexpr const char* kMenuLua = R"(
local function on_release(button, fn)
    local pointer = button.onPointerEvent
    function button:onPointerEvent(...)
        local results = {pointer(self, ...)}
        if results[1] == self.returnValue then fn() end
        return unpack(results)
    end
    return button
end

local init = MainMenu.init
function MainMenu:init(...)
    local results = {init(self, ...)}
    local LM = _G.levelMaker
    if LM and LM.Hub then
        local card = ui.ImageButton:new({name = 'levelMakerButton'})
        card:setImage('DC_FEATURE_ICON_BG')
        card.returnValue = 'LEVEL_MAKER'
        card.clickSound = 'menu_confirm'
        local icon = ui.Image:new()
        icon:setImage('IGC_KINGSLING')
        icon.x, icon.y = 0, 10
        icon:setScale(1.45)
        card:addChild(icon)
        local title = ui.Text:new({font = LM.UI.font('FONT_HEADER'), hanchor = 'HCENTER'})
        title:setText(LM.UI.text('title'))
        title.x, title.y = 0, -118
        title:setScale(0.62)
        card:addChild(title)
        card.levelMakerTitle = title
        local menu = self
        on_release(card, function() LM.Hub.open(menu) end)
        self:addChild(card)
        LM.Hub.reopen(self)
    end
    return unpack(results)
end

-- The main cards in a row, centred: Episodes, the Daily Challenge card when shown,
-- Mighty League and this one. Set each frame from the cards' sizes (not their
-- positions), so it does not drift and fits with or without the Daily Challenge.
local function centre_cards(menu, frame, play, league, card)
    local n = Scaling and Scaling.getScaleNum and Scaling.getScaleNum() or 1
    local cards = {play}
    local dc = menu:getChild('dcButton')
    if dc and dc.visible ~= false then cards[#cards + 1] = dc end
    cards[#cards + 1] = league
    table.sort(cards, function(a, b) return a.x < b.x end)
    cards[#cards + 1] = card
    local widths, total = {}, 0
    for i, c in ipairs(cards) do
        widths[i] = (c == card and 223 * 0.963 or (c.w or 0)) * n
        total = total + widths[i]
    end
    local gap = math.max(0, math.min(0.03 * screenWidth, (screenWidth * 0.98 - total) / (#cards - 1)))
    local x = (screenWidth - total - gap * (#cards - 1)) * 0.5
    for i, c in ipairs(cards) do
        local to = x + widths[i] * 0.5
        if c == league then
            -- the shine behind it and the spinner go along
            local dx = to - league.x
            for _, o in ipairs(frame and frame.children or {}) do
                if o ~= league and o.image == 'ML_SHINE' then o.x = o.x + dx end
            end
            if menu.leagueButtonSpinner then menu.leagueButtonSpinner.x = to end
        end
        c.x = to
        x = x + widths[i] + gap
    end
end

local update = MainMenu.update
function MainMenu:update(...)
    local results = {update(self, ...)}
    -- back from a level or the editor (the menu may be kept from before)
    if _G.levelMaker and _G.levelMaker.reopen then _G.levelMaker.Hub.reopen(self) end
    local frame = self:getChild('mainButtonsFrame')
    local card, play = self:getChild('levelMakerButton'), self:getChild('playButton')
    local league = frame and frame:getChild('leagueButton') or self:getChild('leagueButton')
    if card and play and league then
        -- the card's top and bottom where the Mighty League card's are
        local ls = league.scaleX or 1
        local s = ls * 0.963
        card.scaleX, card.scaleY = s, s
        if card.minScale == nil and ls > 0.5 then card.minScale, card.maxScale = s * 0.95, s * 1.05 end
        card.y = league.y - 27.75 * ls
        centre_cards(self, frame, play, league, card)
        local title = card.levelMakerTitle
        if title and title.w and title.w > 0 then title:setScale(math.min(0.62, 190 / title.w)) end
    end
    return unpack(results)
end
)";

std::string lua_string(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c >= 32 && c < 127) {
            out += char(c);
        } else {
            char buf[8];
            SDL_snprintf(buf, sizeof buf, "\\%03u", unsigned(c));  // three digits: a digit after it stays apart
            out += buf;
        }
    }
    return out + "\"";
}

// Lua for a reply table: {key = value, ...} from pairs of key and already formed values.
std::string table(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string out = "return {";
    for (const auto& [k, v] : fields) out += k + " = " + v + ", ";
    return out + "}";
}

class LevelMaker final : public slingshot::Mod {
public:
    LevelMaker() {
        recomp::vfs_add_virtual_file(kReply, [this] {
            std::lock_guard lk(mutex_);
            return reply_;
        });
        slingshot::mods::on_script_message(kMessages, [this](const std::string& text) { handle(text); });
        std::string lua = std::string(slingshot::mods::kEachFrameLua) + slingshot::mods::kStatisticsLua +
                          "local ENV = getfenv(1)\n";
        for (const char* part : {lm::kTextsLua, lm::kCoreLua, lm::kUiLua, lm::kHubLua, lm::kEditorLua})
            lua += std::string("do\n") + part + "\nend\n";
        lua += "each_frame(function()\n"
               "    local LM = _G.levelMaker\n"
               "    LM.install(gamelua or ENV)\n"
               "    local later = LM.later\n"
               "    LM.later = nil\n"
               "    if later then later() end\n"
               "end)\n";
        slingshot::mods::append_lua("data/scripts/gamelogic.lua", lua);
        slingshot::mods::append_lua("data/scripts/menus/main_menu.lua", kMenuLua);
    }

    bool on_event(const SDL_Event& e, ModContext& ctx) override {
        if (!editing_) return false;
        if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            float notches = e.wheel.y;
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) notches = -notches;
            if (notches != 0) {
                float x, y;
                game_point(e.wheel.mouse_x, e.wheel.mouse_y, ctx, x, y);
                push("{'wheel', " + number(notches) + ", " + number(x) + ", " + number(y) + "}");
            }
            return true;
        }
        if ((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
            e.button.button == SDL_BUTTON_RIGHT) {
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                float x, y;
                game_point(e.button.x, e.button.y, ctx, x, y);
                push("{'secondary', " + number(x) + ", " + number(y) + "}");
            }
            return true;
        }
        return false;
    }

    void on_frame(ModContext& ctx) override {
        finish_dialog();
        update_camera();
        if (!editing_) {
            keys_.clear();
            return;
        }
        // Keys as a state: other mods may take their events first.
        const bool* state = SDL_GetKeyboardState(nullptr);
        const SDL_Keymod mod = SDL_GetModState();
        const bool ctrl = mod & SDL_KMOD_CTRL;
        const bool shift = mod & SDL_KMOD_SHIFT;
        struct Binding {
            SDL_Scancode key;
            int ctrl;  // 1 with Ctrl, 0 without, -1 either
            const char* name;
            bool repeat;
        };
        static const Binding kBindings[] = {
            {SDL_SCANCODE_DELETE, -1, "DELETE", false}, {SDL_SCANCODE_BACKSPACE, -1, "DELETE", false},
            {SDL_SCANCODE_R, 0, "ROTATE", false},        {SDL_SCANCODE_Q, 0, "ROTATE_LEFT", true},
            {SDL_SCANCODE_E, 0, "ROTATE_RIGHT", true},   {SDL_SCANCODE_Z, 1, "UNDO", false},
            {SDL_SCANCODE_Y, 1, "REDO", false},           {SDL_SCANCODE_D, 1, "COPY", false},
            {SDL_SCANCODE_C, 0, "COPY", false},           {SDL_SCANCODE_S, 1, "SAVE", false},
            {SDL_SCANCODE_T, 0, "TEST", false},           {SDL_SCANCODE_EQUALS, -1, "ZOOM_IN", true},
            {SDL_SCANCODE_KP_PLUS, -1, "ZOOM_IN", true},  {SDL_SCANCODE_MINUS, -1, "ZOOM_OUT", true},
            {SDL_SCANCODE_KP_MINUS, -1, "ZOOM_OUT", true}, {SDL_SCANCODE_LEFT, -1, "LEFT", true},
            {SDL_SCANCODE_RIGHT, -1, "RIGHT", true},      {SDL_SCANCODE_UP, -1, "UP", true},
            {SDL_SCANCODE_DOWN, -1, "DOWN", true},
        };
        std::vector<SDL_Scancode> down;
        for (const Binding& b : kBindings) {
            if (!state[b.key] || (b.ctrl == 1 && !ctrl) || (b.ctrl == 0 && ctrl)) continue;
            down.push_back(b.key);
            const bool was = std::find(keys_.begin(), keys_.end(), b.key) != keys_.end();
            if (!was || b.repeat) {
                const char* name = b.name;
                if (b.key == SDL_SCANCODE_Z && shift) name = "REDO";
                push(std::string("{'key', '") + name + "'}");
            }
        }
        keys_ = down;
    }

    // The camera's picture where the scanner page has its frame.
    void on_overlay(slingshot::Overlay& overlay, ModContext& ctx) override {
        float x, y, w, h;
        {
            std::lock_guard lk(mutex_);
            if (preview_frames_ <= 0) return;
            --preview_frames_;
            x = preview_[0], y = preview_[1], w = preview_[2], h = preview_[3];
        }
        if (camera_.state() != lm::QrCamera::State::Running || camera_.pixels().empty()) return;
        const float scale = ctx.screen_scale > 0 ? ctx.screen_scale : 1;
        x = ctx.screen.x + x * scale, y = ctx.screen.y + y * scale, w *= scale, h *= scale;
        // the whole picture, as large as fits
        const bool side = camera_.turns() % 2 == 1;
        const float pw = float(side ? camera_.height() : camera_.width());
        const float ph = float(side ? camera_.width() : camera_.height());
        const float f = std::min(w / pw, h / ph);
        overlay.image(camera_.pixels().data(), camera_.width(), camera_.height(), camera_.generation(),
                      x + (w - pw * f) * 0.5f, y + (h - ph * f) * 0.5f, pw * f, ph * f, camera_.turns());
    }

private:
    static std::string number(double v) {
        char buf[32];
        SDL_snprintf(buf, sizeof buf, "%.3f", v);
        return buf;
    }

    static void game_point(float wx, float wy, const ModContext& ctx, float& x, float& y) {
        const float scale = ctx.screen_scale > 0 ? ctx.screen_scale : 1;
        x = (wx * ctx.pixel_density - ctx.screen.x) / scale;
        y = (wy * ctx.pixel_density - ctx.screen.y) / scale;
    }

    void push(const std::string& event) {
        std::lock_guard lk(mutex_);
        events_.push_back(event);
    }

    void reply(const std::string& lua) {
        std::lock_guard lk(mutex_);
        reply_ = lua;
    }

    // A message of the script: a command, a line break, its argument.
    void handle(const std::string& text) {
        const size_t nl = text.find('\n');
        const std::string cmd = text.substr(0, nl);
        const std::string arg = nl == std::string::npos ? std::string() : text.substr(nl + 1);
        std::string error;
        if (cmd == "encode") {
            const auto code = lm::encode(arg, error);
            reply(code ? table({{"code", lua_string(*code)}}) : table({{"error", lua_string(error)}}));
        } else if (cmd == "decode") {
            const auto level = lm::decode(arg, error);
            reply(level ? table({{"text", lua_string(*level)}}) : table({{"error", lua_string(error)}}));
        } else if (cmd == "qr") {
            int size = 0;
            const std::vector<uint8_t> modules = lm::qr_modules(arg, size);
            std::string bits(modules.size(), '0');
            for (size_t i = 0; i < modules.size(); ++i)
                if (modules[i]) bits[i] = '1';
            reply(table({{"size", std::to_string(size)}, {"modules", lua_string(bits)}}));
        } else if (cmd == "copy") {
#ifdef __SWITCH__
            reply(table({{"ok", "false"}}));
#else
            reply(table({{"ok", SDL_SetClipboardText(arg.c_str()) ? "true" : "false"}}));
#endif
        } else if (cmd == "paste") {
            std::string clip;
#ifndef __SWITCH__
            if (char* t = SDL_GetClipboardText()) {
                clip = t;
                SDL_free(t);
            }
#endif
            if (clip.size() > (4u << 20)) clip.clear();
            reply(table({{"text", lua_string(clip)}}));
        } else if (cmd == "open_picture" || cmd == "save_picture") {
            start_dialog(cmd == "save_picture", arg);
        } else if (cmd == "result") {
            std::lock_guard lk(mutex_);
            if (dialog_ == Dialog::Done) {
                reply_ = result_;
                dialog_ = Dialog::None;
            } else {
                reply_ = dialog_ == Dialog::None ? table({{"error", "'failed'"}}) : table({{"pending", "true"}});
            }
        } else if (cmd == "camera_available") {
            std::lock_guard lk(mutex_);
            if (camera_available_ < 0) camera_check_ = true;
            reply_ = camera_available_ < 0 ? table({{"pending", "true"}})
                                           : table({{"ok", camera_available_ ? "true" : "false"}});
        } else if (cmd == "camera_open" || cmd == "camera_close") {
            std::lock_guard lk(mutex_);
            camera_want_ = cmd == "camera_open" ? 1 : 0;
            preview_frames_ = 0;
            camera_text_.reset();
            camera_state_ = cmd == "camera_open" ? "waiting" : "closed";
            reply_ = table({});
        } else if (cmd == "camera") {
            // the preview's frame on the screen (game pixels): x y w h
            std::lock_guard lk(mutex_);
            SDL_sscanf(arg.c_str(), "%f %f %f %f", &preview_[0], &preview_[1], &preview_[2], &preview_[3]);
            preview_frames_ = 3;
            std::vector<std::pair<std::string, std::string>> fields = {{"state", "'" + camera_state_ + "'"}};
            if (camera_text_) fields.push_back({"code", lua_string(*camera_text_)});
            reply_ = table(fields);
        } else if (cmd == "editor") {
            editing_ = arg == "1";
            std::lock_guard lk(mutex_);
            events_.clear();
            reply_ = table({});
        } else if (cmd == "events") {
            std::lock_guard lk(mutex_);
            std::string list = "{";
            for (const std::string& e : events_) list += e + ", ";
            events_.clear();
            reply_ = table({{"list", list + "}"}});
        } else {
            reply(table({}));
        }
    }

    // ---- file dialogs: SDL answers from its event loop
    enum class Dialog { None, Open, Save, Done };

    void start_dialog(bool save, const std::string& code) {
#ifdef __SWITCH__
        reply(table({{"error", "'unsupported'"}}));
#else
        {
            std::lock_guard lk(mutex_);
            if (dialog_ == Dialog::Open || dialog_ == Dialog::Save) {
                reply_ = table({{"pending", "true"}});
                return;
            }
            dialog_ = save ? Dialog::Save : Dialog::Open;
            code_ = code;
            path_.reset();
            reply_ = table({{"pending", "true"}});
        }
        static const SDL_DialogFileFilter kPictures[] = {{"Pictures", "png;jpg;jpeg;bmp;gif"}};
        static const SDL_DialogFileFilter kPng[] = {{"PNG", "png"}};
        SDL_Window* window = SDL_GetKeyboardFocus();
        auto done = [](void* self, const char* const* files, int) {
            auto* me = static_cast<LevelMaker*>(self);
            std::lock_guard lk(me->mutex_);
            me->path_ = std::string(files && files[0] ? files[0] : "");
            me->dialog_answered_ = true;
            if (!files) me->path_ = std::string("\x01");  // an error
        };
        dialog_answered_ = false;
        if (save)
            SDL_ShowSaveFileDialog(done, this, window, kPng, 1, "level.png");
        else
            SDL_ShowOpenFileDialog(done, this, window, kPictures, 1, nullptr, false);
#endif
    }

    // Reads or writes the file once the dialog answered (on the game's thread).
    void finish_dialog() {
        std::string path, code;
        bool save;
        {
            std::lock_guard lk(mutex_);
            if (!dialog_answered_ || !(dialog_ == Dialog::Open || dialog_ == Dialog::Save)) return;
            dialog_answered_ = false;
            path = path_.value_or("");
            code = code_;
            save = dialog_ == Dialog::Save;
        }
        std::string result;
        if (path == "\x01") {
            result = table({{"error", "'unsupported'"}});
        } else if (path.empty()) {
            result = table({{"cancelled", "true"}});
        } else if (save) {
            if (path.size() < 4 || SDL_strcasecmp(path.c_str() + path.size() - 4, ".png") != 0) path += ".png";
            result = lm::save_qr_png(code, path) ? table({{"ok", "true"}}) : table({{"error", "'failed'"}});
        } else {
            const auto text = lm::read_qr_picture(path);
            if (!text) result = table({{"error", "'no_qr'"}});
            else result = table({{"code", lua_string(*text)}});
        }
        std::lock_guard lk(mutex_);
        result_ = result;
        dialog_ = Dialog::Done;
    }

    // ---- the camera: opened, read and closed on the host's frame
    void update_camera() {
        int want;
        bool check;
        {
            std::lock_guard lk(mutex_);
            want = camera_want_;
            camera_want_ = -1;
            check = camera_check_;
            camera_check_ = false;
        }
        if (check) {
            const bool ok = lm::QrCamera::available();
            std::lock_guard lk(mutex_);
            camera_available_ = ok ? 1 : 0;
        }
        if (want == 1) camera_.open();
        if (want == 0) camera_.close();
        camera_.update();
        using State = lm::QrCamera::State;
        const char* names[] = {"closed", "none", "waiting", "denied", "running"};
        std::lock_guard lk(mutex_);
        if (want != -1 || camera_.state() != State::Closed) camera_state_ = names[int(camera_.state())];
        if (camera_.found() && !camera_text_) camera_text_ = camera_.found();
    }

    lm::QrCamera camera_;
    int camera_want_ = -1;  // 1 open, 0 close
    int camera_available_ = -1;
    bool camera_check_ = false;
    std::string camera_state_ = "closed";
    std::optional<std::string> camera_text_;
    float preview_[4] = {};
    int preview_frames_ = 0;

    std::mutex mutex_;
    std::string reply_ = "return {}";
    std::vector<std::string> events_;
    bool editing_ = false;
    std::vector<SDL_Scancode> keys_;
    Dialog dialog_ = Dialog::None;
    bool dialog_answered_ = false;
    std::optional<std::string> path_;
    std::string code_, result_;
};

}  // namespace

SLINGSHOT_MOD(LevelMaker, "level-maker", "Build, play and share levels of your own (a main menu button)");
