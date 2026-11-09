/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "stackchan_display.h"
#include "sdkconfig.h"
#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>
#include <esp_psram.h>
#include <vector>
#include <cstring>
#include <src/misc/cache/lv_cache.h>
#include <settings.h>
#include <lvgl.h>
#include <lvgl_theme.h>
#include <stackchan/stackchan.h>
#include <assets/lang_config.h>
#include <hal/hal.h>

using namespace stackchan;
using namespace stackchan::avatar;

#define TAG "StackChanAvatarDisplay"

static bool _is_xiaozhi_ready = false;
static bool _is_xiaozhi_idle  = false;
static StackChanAvatarDisplay* s_stackchan_display = nullptr;

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_material_symbols_30_4);

// Have to register themes, so the asset apply can update the text font
void StackChanAvatarDisplay::InitializeLcdThemes()
{
    auto text_font       = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font       = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_material_symbols_30_4);

    // light theme
    auto light_theme = new LvglTheme("light");
    light_theme->set_background_color(lv_color_hex(0xFFFFFF));        // rgb(255, 255, 255)
    light_theme->set_text_color(lv_color_hex(0x000000));              // rgb(0, 0, 0)
    light_theme->set_chat_background_color(lv_color_hex(0xE0E0E0));   // rgb(224, 224, 224)
    light_theme->set_user_bubble_color(lv_color_hex(0x00FF00));       // rgb(0, 128, 0)
    light_theme->set_assistant_bubble_color(lv_color_hex(0xDDDDDD));  // rgb(221, 221, 221)
    light_theme->set_system_bubble_color(lv_color_hex(0xFFFFFF));     // rgb(255, 255, 255)
    light_theme->set_system_text_color(lv_color_hex(0x000000));       // rgb(0, 0, 0)
    light_theme->set_border_color(lv_color_hex(0x000000));            // rgb(0, 0, 0)
    light_theme->set_low_battery_color(lv_color_hex(0x000000));       // rgb(0, 0, 0)
    light_theme->set_text_font(text_font);
    light_theme->set_icon_font(icon_font);
    light_theme->set_large_icon_font(large_icon_font);

    // dark theme
    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_background_color(lv_color_hex(0x000000));        // rgb(0, 0, 0)
    dark_theme->set_text_color(lv_color_hex(0xFFFFFF));              // rgb(255, 255, 255)
    dark_theme->set_chat_background_color(lv_color_hex(0x1F1F1F));   // rgb(31, 31, 31)
    dark_theme->set_user_bubble_color(lv_color_hex(0x00FF00));       // rgb(0, 128, 0)
    dark_theme->set_assistant_bubble_color(lv_color_hex(0x222222));  // rgb(34, 34, 34)
    dark_theme->set_system_bubble_color(lv_color_hex(0x000000));     // rgb(0, 0, 0)
    dark_theme->set_system_text_color(lv_color_hex(0xFFFFFF));       // rgb(255, 255, 255)
    dark_theme->set_border_color(lv_color_hex(0xFFFFFF));            // rgb(255, 255, 255)
    dark_theme->set_low_battery_color(lv_color_hex(0xFF0000));       // rgb(255, 0, 0)
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("light", light_theme);
    theme_manager.RegisterTheme("dark", dark_theme);
}

StackChanAvatarDisplay* StackChanAvatarDisplay::Instance()
{
    return s_stackchan_display;
}

StackChanAvatarDisplay::StackChanAvatarDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                                               int width, int height, int offset_x, int offset_y, bool mirror_x,
                                               bool mirror_y, bool swap_xy)
    : LvglDisplay(), panel_io_(panel_io), panel_(panel)
{
    s_stackchan_display = this;
    width_  = width;
    height_ = height;

    // Initialize LCD themes
    InitializeLcdThemes();

    // Load theme from settings
    Settings settings("display", false);
    std::string theme_name = settings.GetString("theme", "light");
    current_theme_         = LvglThemeManager::GetInstance().GetTheme(theme_name);

    // Draw white screen
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    {
        esp_err_t __err = esp_lcd_panel_disp_on_off(panel_, true);
        if (__err == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "Panel does not support disp_on_off; assuming ON");
        } else {
            ESP_ERROR_CHECK(__err);
        }
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

#if CONFIG_SPIRAM
    // lv image cache, currently only PNG is supported
    size_t psram_size_mb = esp_psram_get_size() / 1024 / 1024;
    if (psram_size_mb >= 8) {
        lv_image_cache_resize(2 * 1024 * 1024, true);
        ESP_LOGI(TAG, "Use 2MB of PSRAM for image cache");
    } else if (psram_size_mb >= 2) {
        lv_image_cache_resize(512 * 1024, true);
        ESP_LOGI(TAG, "Use 512KB of PSRAM for image cache");
    }
#endif

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    // port_cfg.task_priority   = 20;
    port_cfg.task_priority = 3;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle      = panel_io_,
        .panel_handle   = panel_,
        .control_handle = nullptr,
        .buffer_size    = static_cast<uint32_t>(width_ * 20),
        .double_buffer  = false,
        .trans_size     = 0,
        .hres           = static_cast<uint32_t>(width_),
        .vres           = static_cast<uint32_t>(height_),
        .monochrome     = false,
        .rotation =
            {
                .swap_xy  = swap_xy,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags =
            {
                .buff_dma     = 1,
                .buff_spiram  = 0,
                .sw_rotate    = 0,
                .swap_bytes   = 1,
                .full_refresh = 0,
                .direct_mode  = 0,
            },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    // Create a timer to hide the preview image
    esp_timer_create_args_t preview_timer_args = {
        .callback =
            [](void* arg) {
                StackChanAvatarDisplay* display = static_cast<StackChanAvatarDisplay*>(arg);
                display->SetPreviewImage(nullptr);
            },
        .arg                   = this,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "preview_timer",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&preview_timer_args, &preview_timer_);

    esp_timer_create_args_t sleepy_timer_args = {
        .callback =
            [](void* arg) {
                auto* display = static_cast<StackChanAvatarDisplay*>(arg);
                display->EnterStandbySleepy();
            },
        .arg                   = this,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "standby_sleepy",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&sleepy_timer_args, &standby_sleepy_timer_);

    // Create boot logo label if not warm boot
    if (GetHAL().getWarmRebootTarget() < 0) {
        ESP_LOGI(TAG, "Create boot logo label");
        Lock();
        {
            uitk::lvgl_cpp::ScreenActive screen;
            screen.setBgColor(lv_color_hex(0x000000));
        }
        GetHAL().bootLogo = std::make_unique<BootLogo>();
        Unlock();
    }

    // Robot will be created later in SetupXiaoZhiUI()
}

StackChanAvatarDisplay::~StackChanAvatarDisplay()
{
    ESP_LOGI(TAG, "Destroying StackChanAvatarDisplay");

    if (s_stackchan_display == this) {
        s_stackchan_display = nullptr;
    }

    CancelStandbySleepyTimer();
    if (standby_sleepy_timer_ != nullptr) {
        esp_timer_delete(standby_sleepy_timer_);
        standby_sleepy_timer_ = nullptr;
    }

    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
        esp_timer_delete(preview_timer_);
    }

    if (preview_image_ != nullptr) {
        lv_obj_del(preview_image_);
    }

    auto& stackchan = GetStackChan();
    if (stackchan.hasAvatar()) {
        stackchan.resetAvatar();
    }
}

void StackChanAvatarDisplay::CancelStandbySleepyTimer()
{
    if (standby_sleepy_timer_ != nullptr) {
        esp_timer_stop(standby_sleepy_timer_);
    }
}

void StackChanAvatarDisplay::ArmStandbySleepyTimer(int delay_sec)
{
    CancelStandbySleepyTimer();
    int sec = delay_sec;
    if (sec < 0) {
        sec = CONFIG_STACKCHAN_STANDBY_SLEEPY_AFTER_SEC;
    }
    if (sec <= 0 || standby_sleepy_timer_ == nullptr) {
        return;
    }
    const uint64_t delay_us = static_cast<uint64_t>(sec) * 1000000ULL;
    esp_err_t err = esp_timer_start_once(standby_sleepy_timer_, delay_us);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to arm standby sleepy timer: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Standby sleepy in %d s", sec);
}

void StackChanAvatarDisplay::EnterStandbySleepy()
{
    DisplayLockGuard lock(this);
    if (!_is_xiaozhi_idle || is_sleeping_) {
        return;
    }
    ESP_LOGI(TAG, "Standby timeout → sleepy");
    ApplyEmotionNow("sleepy");
}

void StackChanAvatarDisplay::NudgeAfterIdleInteraction()
{
    DisplayLockGuard lock(this);
    if (!_is_xiaozhi_idle) {
        return;
    }
    // Leave sleepy visuals; treat the touch/shake as a short awake window.
    ApplyEmotionNow("neutral");
    GetStackChan().avatar().clearSpeech();
    int delay_sec = CONFIG_STACKCHAN_STANDBY_SLEEPY_AFTER_INTERACTION_SEC;
    if (delay_sec <= 0) {
        delay_sec = CONFIG_STACKCHAN_STANDBY_SLEEPY_AFTER_SEC;
    }
    ArmStandbySleepyTimer(delay_sec);
}

void StackChanAvatarDisplay::SetPowerSaveMode(bool on)
{
    // Keep StackChan sleepy/Zzz path (not only generic LVGL emoji).
    if (on) {
        SetChatMessage("system", "");
        SetEmotion("sleepy");
    } else {
        CancelStandbySleepyTimer();
        SetChatMessage("system", "");
        SetEmotion("neutral");
    }
}

bool StackChanAvatarDisplay::Lock(int timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void StackChanAvatarDisplay::Unlock()
{
    lvgl_port_unlock();
}

lv_disp_t* StackChanAvatarDisplay::GetLvglDisplay()
{
    return display_;
}

#include <hal/board/hal_bridge.h>

void StackChanAvatarDisplay::SetupUI()
{
    // Prevent duplicate calls - if already called, return early
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    Display::SetupUI();  // Mark SetupUI as called

    auto& stackchan = GetStackChan();

    if (stackchan.hasAvatar()) {
        ESP_LOGW(TAG, "Avatar already created");
        return;
    }

    DisplayLockGuard lock(this);

    ESP_LOGI(TAG, "Creating Stack-chan Avatar...");

    auto avatar = std::make_unique<DefaultAvatar>();
    avatar->init(lv_screen_active());
    avatar->getPanel()->onClick().connect([]() {
        static uint32_t last_toggle_tick = 0;
        const uint32_t now               = GetHAL().millis();
        if (last_toggle_tick != 0 && now - last_toggle_tick < 2000) {
            return;
        }

        if (hal_bridge::is_xiaozhi_ready()) {
            last_toggle_tick = now;
            hal_bridge::toggle_xiaozhi_chat_state();
        }
    });

    stackchan.attachAvatar(std::move(avatar));
    stackchan.addModifier(std::make_unique<BreathModifier>());
    blink_modifier_id_ = stackchan.addModifier(std::make_unique<BlinkModifier>());
    stackchan.addModifier(std::make_unique<HeadPetModifier>());
    stackchan.addModifier(std::make_unique<ImuEventModifier>());

    preview_image_ = lv_image_create(lv_screen_active());
    lv_obj_set_size(preview_image_, 320, 240);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    // GetHAL().startStackChanAutoUpdate(24);

    auto config        = hal_bridge::get_xiaozhi_config();
    idle_motion_level_ = config.idleRandomMovementLevel;

    ESP_LOGI(TAG, "Avatar created and started");
}

void StackChanAvatarDisplay::LvglLock()
{
    if (!Lock(30000)) {
        ESP_LOGE("Display", "Failed to lock display");
    }
}

void StackChanAvatarDisplay::LvglUnlock()
{
    Unlock();
}

void StackChanAvatarDisplay::CreateIdleMotionModifier()
{
    auto& stackchan = GetStackChan();

    switch (idle_motion_level_) {
        case 0:
            idle_motion_modifier_id_ = -1;
            return;
        case 1:
            idle_motion_modifier_id_ = stackchan.addModifier(std::make_unique<IdleMotionModifier>(8000, 12000));
            return;
        case 3:
            idle_motion_modifier_id_ = stackchan.addModifier(std::make_unique<IdleMotionModifier>(2000, 4000));
            return;
        case 2:
        default:
            idle_motion_modifier_id_ = stackchan.addModifier(std::make_unique<IdleMotionModifier>());
            return;
    }
}

void StackChanAvatarDisplay::ResyncBlinkModifier()
{
    auto& stackchan     = GetStackChan();
    auto blink_modifier = static_cast<BlinkModifier*>(stackchan.getModifier(blink_modifier_id_));
    if (blink_modifier) {
        blink_modifier->resyncEyeWeights();
    }
}

void StackChanAvatarDisplay::StopSpeakingModifier()
{
    auto& stackchan = GetStackChan();
    if (speaking_modifier_id_ >= 0) {
        stackchan.removeModifier(speaking_modifier_id_);
        speaking_modifier_id_ = -1;
    }
    if (stackchan.hasAvatar()) {
        stackchan.avatar().mouth().setWeight(0);
    }
    // Restore default servo control policy after speech.
    stackchan.motion().setModifyLock(false);
    stackchan.motion().setAutoAngleSyncEnabled(true);
    stackchan.motion().setAutoTorqueReleaseEnabled(true);
    is_speaking_presentation_ = false;
}

void StackChanAvatarDisplay::BeginSpeakingPresentation()
{
    auto& stackchan = GetStackChan();
    if (!stackchan.hasAvatar()) {
        return;
    }

    FlushPendingForSpeaking();

    if (speaking_modifier_id_ < 0) {
        speaking_modifier_id_ = stackchan.addModifier(std::make_unique<SpeakingModifier>(0, 160, true));
    }
    is_speaking_presentation_ = true;
}

void StackChanAvatarDisplay::ShowThinkingExpression()
{
    auto& stackchan = GetStackChan();
    if (!stackchan.hasAvatar()) {
        return;
    }

    auto& avatar = stackchan.avatar();
    avatar.clearSpeech();
    avatar.setEmotion(Emotion::Doubt);
    // Thinking look: glance aside, slight mouth tilt — no "..." bubble.
    avatar.leftEye().setPosition({18, -8});
    avatar.rightEye().setPosition({18, -8});
    avatar.mouth().setWeight(8);
    avatar.mouth().setRotation(3580);
    ResyncBlinkModifier();
}

void StackChanAvatarDisplay::ClearPendingExpressionState()
{
    pending_emotion_.clear();
    pending_assistant_chat_.clear();
    has_pending_emotion_        = false;
    has_pending_assistant_chat_ = false;
}

void StackChanAvatarDisplay::FlushPendingForSpeaking()
{
    if (has_pending_emotion_) {
        ApplyEmotionNow(pending_emotion_.c_str());
        pending_emotion_.clear();
        has_pending_emotion_ = false;
    }

#if CONFIG_STACKCHAN_SPEECH_SUBTITLES
    if (has_pending_assistant_chat_) {
        GetStackChan().avatar().setSpeech(pending_assistant_chat_);
        pending_assistant_chat_.clear();
        has_pending_assistant_chat_ = false;
    }
#else
    pending_assistant_chat_.clear();
    has_pending_assistant_chat_ = false;
#endif
}

void StackChanAvatarDisplay::ApplyEmotionNow(const char* emotion)
{
    auto& stackchan = GetStackChan();
    if (!stackchan.hasAvatar() || !emotion) {
        return;
    }

    auto& avatar = stackchan.avatar();

    auto leave_sleep = [&]() {
        if (is_sleeping_) {
            avatar.clearSpeech();
            is_sleeping_ = false;
        }
    };

    if (strcmp(emotion, "neutral") == 0) {
        leave_sleep();
        avatar.setEmotion(Emotion::Neutral);
    } else if (strcmp(emotion, "happy") == 0 || strcmp(emotion, "laughing") == 0) {
        leave_sleep();
        avatar.setEmotion(Emotion::Happy);
    } else if (strcmp(emotion, "angry") == 0) {
        leave_sleep();
        avatar.setEmotion(Emotion::Angry);
    } else if (strcmp(emotion, "sad") == 0 || strcmp(emotion, "crying") == 0) {
        leave_sleep();
        avatar.setEmotion(Emotion::Sad);
    } else if (strcmp(emotion, "sleepy") == 0) {
        avatar.setEmotion(Emotion::Sleepy);
        avatar.setSpeech("Zzz…");
        is_sleeping_ = true;

        ESP_LOGW(TAG, "Stop idle motion");
        if (idle_motion_modifier_id_ >= 0) {
            stackchan.removeModifier(idle_motion_modifier_id_);
            idle_motion_modifier_id_ = -1;
        }
        if (idle_expression_modifier_id_ >= 0) {
            stackchan.removeModifier(idle_expression_modifier_id_);
            idle_expression_modifier_id_ = -1;
        }

        stackchan.motion().pitchServo().moveWithSpeed(0, 80);
    } else if (strcmp(emotion, "doubtful") == 0) {
        leave_sleep();
        avatar.setEmotion(Emotion::Doubt);
    } else {
        ESP_LOGW(TAG, "Unknown emotion: %s, using NEUTRAL", emotion);
        leave_sleep();
        avatar.setEmotion(Emotion::Neutral);
    }

    ResyncBlinkModifier();
}

void StackChanAvatarDisplay::SetEmotion(const char* emotion)
{
    auto& stackchan = GetStackChan();

    if (!stackchan.hasAvatar() || !emotion) {
        return;
    }

    DisplayLockGuard lock(this);

    const bool is_system_emotion =
        strcmp(emotion, "neutral") == 0 || strcmp(emotion, "sleepy") == 0 || strcmp(emotion, "robot_2") == 0;

    // LLM emotion / TTS start both arrive before audible audio. Hold expressive
    // emotions until the first sentence_start (BeginSpeakingPresentation).
    if (!is_speaking_presentation_ && !is_system_emotion) {
        pending_emotion_     = emotion;
        has_pending_emotion_ = true;
        ShowThinkingExpression();
        return;
    }

    if (!is_speaking_presentation_ && (is_waiting_response_ || is_speaking_session_) &&
        strcmp(emotion, "neutral") == 0) {
        ShowThinkingExpression();
        return;
    }

    if (is_system_emotion) {
        has_pending_emotion_ = false;
        pending_emotion_.clear();
    }

    ApplyEmotionNow(emotion);
}

void StackChanAvatarDisplay::SetChatMessage(const char* role, const char* content)
{
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetChatMessage('%s', '%s') called before SetupUI() - message will be lost!", role, content);
    }

    auto& stackchan = GetStackChan();
    if (!stackchan.hasAvatar() || !role) {
        return;
    }

    DisplayLockGuard lock(this);

    const char* text = content ? content : "";

    if (strcmp(role, "system") == 0) {
        // Don't wipe the thinking bubble while waiting for a reply.
        if ((is_waiting_response_ || is_speaking_session_) && text[0] == '\0') {
            return;
        }
        stackchan.avatar().setSpeech(text);
        return;
    }

    if (strcmp(role, "assistant") == 0) {
        // sentence_start is the closest UI signal to actual spoken audio.
        if (is_speaking_session_) {
            if (!is_speaking_presentation_) {
#if CONFIG_STACKCHAN_SPEECH_SUBTITLES
                pending_assistant_chat_     = text;
                has_pending_assistant_chat_ = true;
#endif
                BeginSpeakingPresentation();
            }
#if CONFIG_STACKCHAN_SPEECH_SUBTITLES
            else {
                stackchan.avatar().setSpeech(text);
            }
#endif
            return;
        }

#if CONFIG_STACKCHAN_SPEECH_SUBTITLES
        pending_assistant_chat_     = text;
        has_pending_assistant_chat_ = true;
#endif
        if (is_waiting_response_ || has_pending_emotion_ || text[0] != '\0') {
            ShowThinkingExpression();
        }
    }
}

void StackChanAvatarDisplay::ClearChatMessages()
{
    auto& stackchan = GetStackChan();
    if (!stackchan.hasAvatar()) {
        return;
    }

    DisplayLockGuard lock(this);

    has_pending_assistant_chat_ = false;
    pending_assistant_chat_.clear();
    stackchan.avatar().clearSpeech();

    ESP_LOGI(TAG, "Chat messages cleared");
}

void StackChanAvatarDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image)
{
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) {
        return;
    }

    if (image == nullptr) {
        esp_timer_stop(preview_timer_);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        preview_image_cached_.reset();
        return;
    }

    preview_image_cached_ = std::move(image);
    auto img_dsc          = preview_image_cached_->image_dsc();
    // Set image source and show preview image
    lv_image_set_src(preview_image_, img_dsc);
    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
        // Scale to fit width
        lv_image_set_scale(preview_image_, 256 * width_ / img_dsc->header.w);
    }

    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(preview_image_);
    esp_timer_stop(preview_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(preview_timer_, 6000 * 1000));
}

void StackChanAvatarDisplay::UpdateStatusBar(bool update_all)
{
}

void StackChanAvatarDisplay::SetTheme(Theme* theme)
{
    ESP_LOGI(TAG, "SetTheme: %s", theme->name().c_str());

    auto& stackchan = GetStackChan();
    if (!stackchan.hasAvatar()) {
        ESP_LOGE(TAG, "Avatar is invalid");
        return;
    }

    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    auto text_font  = lvgl_theme->text_font()->font();

    stackchan.avatar().setSpeechTextFont((void*)text_font);
}

#include <hal/board/hal_bridge.h>
bool hal_bridge::is_xiaozhi_ready()
{
    return _is_xiaozhi_ready;
}
bool hal_bridge::is_xiaozhi_idle()
{
    return _is_xiaozhi_idle;
}
void hal_bridge::nudge_after_idle_interaction()
{
    if (auto* display = StackChanAvatarDisplay::Instance()) {
        display->NudgeAfterIdleInteraction();
    }
}

void StackChanAvatarDisplay::SetStatus(const char* status)
{
    // ESP_LOGE(TAG, "SetStatus: %s", status);

    auto& stackchan = GetStackChan();
    if (!stackchan.hasAvatar()) {
        ESP_LOGE(TAG, "Avatar is invalid");
        return;
    }

    auto& avatar = stackchan.avatar();

    DisplayLockGuard lock(this);

    bool is_idle = false;

    if (strcmp(status, Lang::Strings::LISTENING) == 0) {
        is_speaking_session_      = false;
        is_speaking_presentation_ = false;
        is_waiting_response_      = false;
        ClearPendingExpressionState();
        StopSpeakingModifier();

        GetHAL().setRgbColor(0, 0, 50, 0);
        GetHAL().refreshRgb();

    } else if (strcmp(status, Lang::Strings::STANDBY) == 0) {
        _is_xiaozhi_ready = true;

        is_speaking_session_      = false;
        is_speaking_presentation_ = false;
        is_waiting_response_      = false;
        ClearPendingExpressionState();
        StopSpeakingModifier();

        is_idle = true;

        GetHAL().setRgbColor(0, 0, 0, 0);
        GetHAL().refreshRgb();

    } else if (strcmp(status, Lang::Strings::SPEAKING) == 0) {
        // tts start arrives before decoded PCM reaches the speaker. Keep the
        // thinking face until sentence_start kicks BeginSpeakingPresentation().
        is_speaking_session_ = true;
        is_waiting_response_ = false;
        if (!is_speaking_presentation_) {
            ShowThinkingExpression();
        }

        GetHAL().setRgbColor(0, 0, 0, 50);
        GetHAL().refreshRgb();

    } else if (strcmp(status, Lang::Strings::CONNECTING) == 0) {
        is_speaking_session_      = false;
        is_speaking_presentation_ = false;
        is_waiting_response_      = true;
        StopSpeakingModifier();
        ShowThinkingExpression();

    } else if (strcmp(status, Lang::Strings::CHECKING_NEW_VERSION) == 0 ||
               strcmp(status, Lang::Strings::OTA_UPGRADE) == 0) {
        // Custom StackChan firmware: never surface OTA check / upgrade UI.
        return;

    } else {
        is_speaking_session_      = false;
        is_speaking_presentation_ = false;
        is_waiting_response_      = false;
        avatar.setSpeech(status);
    }

    if (is_idle) {
        // Face-only idle micro-expression. Servo idle roam is disabled so the
        // head only moves while speaking.
        ESP_LOGW(TAG, "Start idle expression");
        if (idle_motion_modifier_id_ >= 0) {
            stackchan.removeModifier(idle_motion_modifier_id_);
            idle_motion_modifier_id_ = -1;
        }

        _is_xiaozhi_idle = true;
        if (is_sleeping_) {
            // Already in standby-sleepy: keep face, don't fight with idle micro-expr.
            if (idle_expression_modifier_id_ >= 0) {
                stackchan.removeModifier(idle_expression_modifier_id_);
                idle_expression_modifier_id_ = -1;
            }
        } else {
            if (idle_expression_modifier_id_ < 0) {
                idle_expression_modifier_id_ = stackchan.addModifier(std::make_unique<IdleExpressionModifier>());
            }
            // Arm after idle flag is set so the callback won't no-op.
            ArmStandbySleepyTimer();
        }
    } else {
        CancelStandbySleepyTimer();
        ESP_LOGW(TAG, "Stop idle expression");
        if (idle_motion_modifier_id_ >= 0) {
            stackchan.removeModifier(idle_motion_modifier_id_);
            idle_motion_modifier_id_ = -1;
        }
        if (idle_expression_modifier_id_ >= 0) {
            stackchan.removeModifier(idle_expression_modifier_id_);
            idle_expression_modifier_id_ = -1;
        }

        // Wake word / listen / speak: leave standby Sleepy+Zzz.
        if (is_sleeping_) {
            ApplyEmotionNow("neutral");
        }

        _is_xiaozhi_idle = false;
    }
}

void StackChanAvatarDisplay::ShowNotification(const char* notification, int duration_ms)
{
}
