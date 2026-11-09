/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <display/lvgl_display/lvgl_display.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_timer.h>
#include <memory>
#include <string>

class StackChanAvatarDisplay : public LvglDisplay {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_       = nullptr;
    int speaking_modifier_id_           = -1;
    int idle_motion_modifier_id_        = -1;
    int idle_expression_modifier_id_    = -1;
    int blink_modifier_id_              = -1;
    bool is_sleeping_                   = false;
    bool is_speaking_session_           = false;  // tts start..stop
    bool is_speaking_presentation_      = false;  // first sentence / audio-aligned UI
    bool is_waiting_response_           = false;
    uint8_t idle_motion_level_          = 2;
    std::string pending_emotion_;
    std::string pending_assistant_chat_;
    bool has_pending_emotion_           = false;
    bool has_pending_assistant_chat_    = false;

    lv_obj_t* preview_image_                         = nullptr;
    esp_timer_handle_t preview_timer_                = nullptr;
    esp_timer_handle_t standby_sleepy_timer_         = nullptr;
    std::unique_ptr<LvglImage> preview_image_cached_ = nullptr;

    void CreateIdleMotionModifier();
    void ApplyEmotionNow(const char* emotion);
    void ShowThinkingExpression();
    void ClearPendingExpressionState();
    void FlushPendingForSpeaking();
    void ResyncBlinkModifier();
    void StopSpeakingModifier();
    void BeginSpeakingPresentation();
    void ArmStandbySleepyTimer(int delay_sec = -1);
    void CancelStandbySleepyTimer();
    void EnterStandbySleepy();

protected:
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

public:
    StackChanAvatarDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height,
                           int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy);
    virtual ~StackChanAvatarDisplay();

    void InitializeLcdThemes();

    // Override Display methods to control Robot
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void UpdateStatusBar(bool update_all = false) override;
    virtual void SetupUI() override;
    virtual void SetTheme(Theme* theme) override;
    virtual void SetStatus(const char* status) override;
    virtual void SetPowerSaveMode(bool on) override;
    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override;

    void LvglLock();
    void LvglUnlock();
    lv_disp_t* GetLvglDisplay();

    static StackChanAvatarDisplay* Instance();
    // After pet/dizzy while idle: Neutral + shorter sleepy re-arm.
    void NudgeAfterIdleInteraction();
};
