
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

## Update history

- **Speech subtitles switch**: added `CONFIG_STACKCHAN_SPEECH_SUBTITLES` under menuconfig
  `StackChan Display`, so speaking captions can be turned on/off without editing code
  (rebuild + flash after changing).
