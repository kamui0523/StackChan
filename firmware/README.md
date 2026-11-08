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

## Speech subtitles (说话字幕开关)

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

## Speaking head motion (说话时晃头间隔)

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

## Update history

- **Speech subtitles switch**: added `CONFIG_STACKCHAN_SPEECH_SUBTITLES` under menuconfig
`StackChan Display`, so speaking captions can be turned on/off without editing code
(rebuild + flash after changing).
- **Speaking head motion interval**: default speaking wander retarget interval set to
800ms to reduce UART contention with TTS.
- **Conversation idle / re-wake**: documented that returning to Idle after silence is
driven by cloud `goodbye`, not a local silence timer; wake word is required again.
