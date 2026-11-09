## Build

### Fetch Dependencies

```bash
python3 ./fetch_repos.py
```

### Tool Chains

[ESP-IDF v6.1](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/index.html)

### Build

```bash
idf.py build
```

### Host-side tests

The motion coordinate helpers can be tested without ESP-IDF hardware:

```bash
cmake -S tests -B build-host-tests
cmake --build build-host-tests
ctest --test-dir build-host-tests --output-on-failure
```

### Flash

```bash
idf.py flash
```

## Speech subtitles（说话字幕开关）

Compile-time switch for the on-screen TTS speech bubble while the robot is speaking.
Face / mouth animation still runs when subtitles are off.

**menuconfig**

```bash
idf.py menuconfig
```

Go to **StackChan Display** → **Show speech subtitles while speaking**, then rebuild and flash.

**sdkconfig / sdkconfig.defaults**

- Enable: `CONFIG_STACKCHAN_SPEECH_SUBTITLES=y`
- Disable: `# CONFIG_STACKCHAN_SPEECH_SUBTITLES is not set`

## Speaking head motion（说话时晃头间隔）

Speaking-time head wander uses the servo UART. Retargeting too often can contend with
the audio task and make TTS sound stuttery.

The update interval controls how often the head pose is retargeted while speaking:

- File: `main/stackchan/modifiers/speaking.h`
- Constant: `_motion_interval_ms`
- **Current value: 800ms** (was about 280ms)

A longer interval means fewer bus writes and usually smoother speech; head motion also
changes less often. Amplitude and speed keep the previous larger values; only the
interval was changed.

Rebuild and flash after editing:

```bash
idf.py build flash
```

## Conversation idle / re-wake（不说话后回到待机状态）

After wake word and entering a conversation, if you stay silent for a while the device
returns to **Idle** and you must say the wake word again (currently 「你好小智」) to chat.

This is **not** a local “N seconds of silence” timer in firmware:

1. Default listening mode is **AutoStop** (when device-side AEC is off)
2. The cloud ends the session and sends `goodbye`
3. The device closes the audio channel → **Idle**, with only wake-word detection left on

How long silence lasts before standby is therefore set by the **Xiaozhi cloud session
timeout**. This repo’s firmware has **no** matching seconds config.

Device-side fallback only: about **120 seconds** with no server packets marks the channel
timed out (cloud `goodbye` usually arrives earlier).

Confirm on the serial log: `Received goodbye message` means the cloud ended the session.

## Standby sleepy face（待机瞌睡表情）

After the device returns to **standby/idle**, firmware can show the **Sleepy** face and
speech bubble `Zzz…` without waiting for a cloud emotion.

**Defaults (stack_chan 1.0.2)**

| Setting | Default | Meaning |
| --- | --- | --- |
| `CONFIG_STACKCHAN_STANDBY_SLEEPY_AFTER_SEC` | `30` | Idle → Sleepy + Zzz after N seconds (`0` disables) |
| `CONFIG_STACKCHAN_STANDBY_SLEEPY_AFTER_INTERACTION_SEC` | `15` | After head-pet / shake while idle: back to Neutral, then Sleepy again after N seconds (`0` = same as the standby delay above) |

**menuconfig**

```bash
idf.py menuconfig
```

Go to **StackChan Display**:

- **Enter sleepy face after standby (seconds)**
- **Re-enter sleepy after head-pet/shake while idle (seconds)**

Then rebuild and flash. Values also live in `sdkconfig.defaults`.

**Behavior notes**

- Wake word / listening / speaking cancels the timer and leaves Sleepy + Zzz.
- Head-pet or shake while sleepy clears Zzz, restores Neutral, then re-arms the shorter
  interaction timer.
- This is independent of cloud 「进入睡觉模式」 emotion handling.

## Update history

### stack_chan 1.0.2（2026-11-09）

- **Standby sleepy（待机瞌睡表情）**: idle/standby 约 30 秒后显示 Sleepy + `Zzz…`
  （`CONFIG_STACKCHAN_STANDBY_SLEEPY_AFTER_SEC`）。
- **Interaction re-sleep（互动后再睡）**: 待机时摸头 / 晃醒后回到 Neutral，再约 15 秒重新
  Sleepy（`CONFIG_STACKCHAN_STANDBY_SLEEPY_AFTER_INTERACTION_SEC`）。

### stack_chan 1.0.1（2026-11-08）

- **Photo head freeze（拍照锁头）**: Capture 拍照时锁定舵机，减轻运动模糊。
- **Speaking wander interval（说话晃头间隔）**: 说话时头部重定向默认改为 800ms，减轻与
  TTS 抢舵机 UART。
- **Cold-boot AI face（冷启动进 AI 脸）**: 冷启动直接进入 AI 对话脸；热重启仍回 launcher。
- **Wake word default（默认唤醒词）**: 默认「你好小智」；说话字幕默认关闭。
- **OTA guard（OTA 防护）**: 跳过远端固件覆盖刷写，仍拉取协议 / 配置。
- **Servo smooth + MCP hold（舵机平滑与 MCP 姿态保持）**: 说话晃头不再覆盖
  `set_head_angles`；`WritePos` 带速度跟踪，并打印指令角与实机角。

### stack_chan 1.0（2026-11-08）

- **SoftAP provisioning（SoftAP 配网）**: 首次配网从 App BLE 改为 SoftAP 热点。
- **xiaozhi.me activation（官方激活码）**: 恢复 xiaozhi.me 激活码绑定，保留 LVGL 几何脸。
- **Speech subtitles switch（说话字幕开关）**: menuconfig `CONFIG_STACKCHAN_SPEECH_SUBTITLES`
  可开关说话字幕（改完需重新编译烧录）。
- **Conversation idle / re-wake（会话待机与再唤醒）**: 沉默后回 Idle 由云端 `goodbye`
  驱动，非本地静音计时；需再次唤醒词才能继续聊。
