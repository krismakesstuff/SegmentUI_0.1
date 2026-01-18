# Build and Upload

## PlatformIO Path

```
C:\Users\krisc\.platformio\penv\Scripts\pio.exe
```

Or in Git Bash / Unix-style:
```
/c/Users/krisc/.platformio/penv/Scripts/pio.exe
```

## Build Commands

**Build only:**
```bash
/c/Users/krisc/.platformio/penv/Scripts/pio.exe run -e esp32doit-devkit-v1
```

**Build and upload via USB:**
```bash
/c/Users/krisc/.platformio/penv/Scripts/pio.exe run -e esp32doit-devkit-v1 -t upload
```

**Build and upload via OTA (wireless):**
```bash
/c/Users/krisc/.platformio/penv/Scripts/pio.exe run -e esp32doit-devkit-v1-ota -t upload
```

## Environments

- `esp32doit-devkit-v1` - USB upload
- `esp32doit-devkit-v1-ota` - OTA upload to 192.168.1.221
