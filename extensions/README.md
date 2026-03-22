# Extensions

This directory contains optional integration scripts that extend `fcitx5-vinput`
without becoming part of the core daemon implementation.

- `extensions/asr/`: external ASR provider scripts
- `extensions/llm/`: LLM/provider bridge scripts

Built-in extensions are installed to
`/usr/share/fcitx5-vinput/extensions/` by default. User extensions can be
placed under `~/.config/vinput/extensions/`, and user files override built-in
extensions with the same script name.

Each script should include a metadata block:

```text
# ==vinput-extension==
# @name         ElevenLabs Speech to Text
# @type         asr
# @description  Cloud ASR via ElevenLabs API
# @author       xifan
# @version      1.0.0
# ==/vinput-extension==
```

`@type` supports:

- `asr`
- `llm`

ASR scripts are intended to be referenced by provider configuration entries.
They should follow the same contract used by the command ASR provider:

- `stdin`: one utterance audio stream from vinput
- `stdout`: final text result
- `stderr`: human-readable error output
- exit code `0`: success
- non-zero exit code: failure

LLM scripts are background proxy services. They can be managed with:

- `vinput extension list`
- `vinput extension start <id>`
- `vinput extension stop <id>`
