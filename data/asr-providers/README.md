# ASR Provider Scripts

This directory contains built-in ASR provider scripts tracked in the project
repository. Build/install steps copy them into the runtime data directory.

- Built-in install path: `/usr/share/fcitx5-vinput/asr-providers/`
- User override path: `~/.config/vinput/asr-providers/`

Managed providers should use an explicit command specification:

- `command`: executable or interpreter
- `args`: script path and additional arguments
- `env`: environment overrides

The optional metadata block format is:

```text
# ==vinput-asr-provider==
# @name         ElevenLabs Speech to Text
# @description  Cloud ASR via ElevenLabs API
# @author       xifan
# @version      1.0.0
# ==/vinput-asr-provider==
```
