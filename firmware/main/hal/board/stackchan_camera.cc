#include "stackchan_camera.h"
#include "assets/assets.h"
#include "hal_bridge.h"
#include <stackchan/stackchan.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

bool StackChanCamera::Capture()
{
    auto& motion                  = GetStackChan().motion();
    const bool was_locked         = motion.isModifyLocked();
    const bool was_auto_torque    = motion.getAutoTorqueReleaseEnabled();

    // Freeze pose: block speaking/IMU wander and stop the current spring.
    motion.setModifyLock(true);
    motion.holdCurrent();
    motion.setAutoTorqueReleaseEnabled(false);
    motion.setTorqueEnabled(true);

    // Short settle so the last SCS WritePos finishes before the shutter.
    vTaskDelay(pdMS_TO_TICKS(150));

    hal_bridge::app_play_sound(OGG_CAMERA_SHUTTER);
    const bool ok = EspVideo::Capture();

    motion.setAutoTorqueReleaseEnabled(was_auto_torque);
    if (!was_locked) {
        motion.setModifyLock(false);
    }

    return ok;
}

bool StackChanCamera::StreamCaptures()
{
    return CaptureFrame(false, 0);
}
