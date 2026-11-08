/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../modifiable.h"
#include "../utils/random.h"
#include <smooth_ui_toolkit.hpp>
#include <hal/hal.h>
#include <cstdint>
#include <cmath>

namespace stackchan {

class SpeakingModifier : public Modifier {
public:
    /**
     * @param destroyAfterMs 持续说话时间（0 为永久，直到手动移除）
     * @param mouthIntervalMs 嘴巴开合频率（默认 160ms）
     * @param enableMotion 是否在说话时伴随头部微动
     */
    SpeakingModifier(uint32_t destroyAfterMs = 0, uint32_t mouthIntervalMs = 160, bool enableMotion = true)
        : _mouth_interval_ms(mouthIntervalMs), _enable_motion(enableMotion)
    {
        uint32_t now = GetHAL().millis();

        if (destroyAfterMs > 0) {
            _destroy_at   = now + destroyAfterMs;
            _has_lifetime = true;
        }

        _next_mouth_tick = now + _mouth_interval_ms;
        if (_enable_motion) {
            // Start motion quickly so the first nod lands with the first syllable.
            _next_motion_tick = now + 120;
        }
    }

    void _update(Modifiable& stackchan) override
    {
        if (!stackchan.hasAvatar()) {
            return;
        }

        if (!_prepared) {
            prepare_motion(stackchan);
        }

        uint32_t now = GetHAL().millis();

        if (_has_lifetime && now >= _destroy_at) {
            stackchan.avatar().mouth().setWeight(0);
            requestDestroy();
            return;
        }

        if (now >= _next_mouth_tick) {
            _next_mouth_tick = now + _mouth_interval_ms;
            animate_mouth(stackchan.avatar());
        }

        if (_enable_motion && now >= _next_motion_tick) {
            // Overlapping retargets keep the spring moving continuously.
            _next_motion_tick = now + 280;
            perform_smooth_speaking_motion(stackchan);
        }
    }

private:
    void prepare_motion(Modifiable& stackchan)
    {
        auto& motion = stackchan.motion();
        // Keep spring momentum and skip torque UART chatter while speaking.
        motion.setAutoAngleSyncEnabled(false);
        motion.setAutoTorqueReleaseEnabled(false);
        motion.setTorqueEnabled(true);

        _base_angles = motion.getCurrentAngles();
        _prepared    = true;
    }

    void animate_mouth(avatar::Avatar& avatar)
    {
        _is_mouth_open = !_is_mouth_open;
        auto& random   = Random::getInstance();

        int weight = _is_mouth_open ? random.getInt(_open_min_weight, _open_max_weight)
                                    : random.getInt(_close_min_weight, _close_max_weight);

        avatar.mouth().setWeight(weight);
    }

    void perform_smooth_speaking_motion(Modifiable& stackchan)
    {
        auto& motion = stackchan.motion();
        if (motion.isModifyLocked()) {
            return;
        }

        _phase_ms += 280;
        const float t = static_cast<float>(_phase_ms) * 0.001f;

        // Smooth Lissajous-style wander around the pose at speech start.
        const float yaw_amp   = 55.0f;
        const float pitch_amp = 40.0f;
        const int32_t target_yaw =
            _base_angles.x + static_cast<int32_t>(std::sin(t * 2.1f) * yaw_amp + std::sin(t * 0.7f) * 18.0f);
        const int32_t target_pitch =
            _base_angles.y + static_cast<int32_t>(std::sin(t * 3.3f + 0.6f) * pitch_amp + std::sin(t * 1.1f) * 12.0f);

        // Mid-high speed → stiffer critical-damped spring, less "step then stop".
        motion.moveWithSpeed(target_yaw, target_pitch, 520);
    }

    const int _open_min_weight  = 40;
    const int _open_max_weight  = 80;
    const int _close_min_weight = 0;
    const int _close_max_weight = 20;

    uint32_t _destroy_at       = 0;
    uint32_t _next_mouth_tick  = 0;
    uint32_t _next_motion_tick = 0;
    uint32_t _mouth_interval_ms;
    uint32_t _phase_ms = 0;

    bool _has_lifetime  = false;
    bool _enable_motion = false;
    bool _is_mouth_open = false;
    bool _prepared      = false;

    uitk::Vector2i _base_angles;
};

}  // namespace stackchan
