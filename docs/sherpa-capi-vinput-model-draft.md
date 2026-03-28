# sherpa-onnx C API Driven `vinput-model.json` Draft

Status: draft

## Goal

Record the current schema direction for `vinput-model.json` when the runtime
consumer is `sherpa-onnx` C API.

This draft intentionally follows C API naming and C API family boundaries.
It does not follow upstream documentation page grouping when those differ from
the actual runtime config structs.

## Agreed Principles

- The runtime contract is `sherpa-onnx` C API, so family classification must
  follow C API structs.
- All parameters that the runtime may need should be expressible in
  `vinput-model.json`.
- Users should be able to adjust parameters by editing `vinput-model.json`
  directly.
- Field names should stay as close as possible to C API field names.
- Documentation categories such as `zipformer-transducer` or `TeleSpeech`
  should not override C API family naming.

## C API Families

### Online

- `transducer`
- `paraformer`
- `zipformer2_ctc`
- `nemo_ctc`
- `t_one_ctc`

### Offline

- `transducer`
- `paraformer`
- `nemo_ctc`
- `whisper`
- `tdnn`
- `sense_voice`
- `moonshine`
- `fire_red_asr`
- `dolphin`
- `zipformer_ctc`
- `canary`
- `wenet_ctc`
- `omnilingual`
- `medasr`
- `funasr_nano`
- `fire_red_asr_ctc`

## Draft Top-Level Shape

```json
{
  "backend": "sherpa-streaming",
  "runtime": "online",
  "family": "transducer",
  "recognizer": {},
  "model": {}
}
```

Rules:

- `backend` identifies which local integration/backend should load the model.
- `runtime` is `online` or `offline`.
- `family` must be one of the C API family names for that runtime.
- `recognizer` maps to `SherpaOnnxOnlineRecognizerConfig` or
  `SherpaOnnxOfflineRecognizerConfig`.
- `model` maps to `SherpaOnnxOnlineModelConfig` or
  `SherpaOnnxOfflineModelConfig`.
- Only the active family block needs to exist inside `model`.
- `telespeech_ctc` remains a direct field under `model`, matching C API.

## Online Draft

```json
{
  "backend": "sherpa-streaming",
  "runtime": "online",
  "family": "transducer",
  "recognizer": {
    "feat_config": {
      "sample_rate": 16000,
      "feature_dim": 80
    },
    "decoding_method": "greedy_search",
    "max_active_paths": 4,
    "enable_endpoint": 0,
    "rule1_min_trailing_silence": 2.4,
    "rule2_min_trailing_silence": 1.2,
    "rule3_min_utterance_length": 20.0,
    "hotwords_file": "",
    "hotwords_score": 1.5,
    "ctc_fst_decoder_config": {
      "graph": "",
      "max_active": 3000
    },
    "rule_fsts": "",
    "rule_fars": "",
    "blank_penalty": 0.0,
    "hotwords_buf": "",
    "hotwords_buf_size": 0,
    "hr": {
      "lexicon": "",
      "rule_fsts": ""
    }
  },
  "model": {
    "tokens": "tokens.txt",
    "num_threads": 1,
    "provider": "cpu",
    "debug": 0,
    "model_type": "",
    "modeling_unit": "",
    "bpe_vocab": "",
    "tokens_buf": "",
    "tokens_buf_size": 0,
    "transducer": {
      "encoder": "encoder.onnx",
      "decoder": "decoder.onnx",
      "joiner": "joiner.onnx"
    }
  }
}
```

### Online Family-Specific Blocks

`transducer`

```json
{
  "encoder": "encoder.onnx",
  "decoder": "decoder.onnx",
  "joiner": "joiner.onnx"
}
```

`paraformer`

```json
{
  "encoder": "encoder.onnx",
  "decoder": "decoder.onnx"
}
```

`zipformer2_ctc`

```json
{
  "model": "model.onnx"
}
```

`nemo_ctc`

```json
{
  "model": "model.onnx"
}
```

`t_one_ctc`

```json
{
  "model": "model.onnx"
}
```

## Offline Draft

```json
{
  "backend": "sherpa-offline",
  "runtime": "offline",
  "family": "whisper",
  "recognizer": {
    "feat_config": {
      "sample_rate": 16000,
      "feature_dim": 80
    },
    "lm_config": {
      "model": "",
      "scale": 0.0
    },
    "decoding_method": "greedy_search",
    "max_active_paths": 4,
    "hotwords_file": "",
    "hotwords_score": 1.5,
    "rule_fsts": "",
    "rule_fars": "",
    "blank_penalty": 0.0,
    "hr": {
      "lexicon": "",
      "rule_fsts": ""
    }
  },
  "model": {
    "tokens": "tokens.txt",
    "num_threads": 1,
    "debug": 0,
    "provider": "cpu",
    "model_type": "",
    "modeling_unit": "",
    "bpe_vocab": "",
    "telespeech_ctc": "",
    "whisper": {
      "encoder": "encoder.onnx",
      "decoder": "decoder.onnx",
      "language": "en",
      "task": "transcribe",
      "tail_paddings": 0,
      "enable_token_timestamps": 0,
      "enable_segment_timestamps": 0
    }
  }
}
```

### Offline Family-Specific Blocks

`transducer`

```json
{
  "encoder": "encoder.onnx",
  "decoder": "decoder.onnx",
  "joiner": "joiner.onnx"
}
```

`paraformer`

```json
{
  "model": "model.onnx"
}
```

`nemo_ctc`

```json
{
  "model": "model.onnx"
}
```

`whisper`

```json
{
  "encoder": "encoder.onnx",
  "decoder": "decoder.onnx",
  "language": "en",
  "task": "transcribe",
  "tail_paddings": 0,
  "enable_token_timestamps": 0,
  "enable_segment_timestamps": 0
}
```

`tdnn`

```json
{
  "model": "model.onnx"
}
```

`sense_voice`

```json
{
  "model": "model.onnx",
  "language": "auto",
  "use_itn": 1
}
```

`moonshine`

```json
{
  "preprocessor": "preprocessor.onnx",
  "encoder": "encoder.onnx",
  "uncached_decoder": "uncached_decoder.onnx",
  "cached_decoder": "cached_decoder.onnx",
  "merged_decoder": "merged_decoder.onnx"
}
```

`fire_red_asr`

```json
{
  "encoder": "encoder.onnx",
  "decoder": "decoder.onnx"
}
```

`dolphin`

```json
{
  "model": "model.onnx"
}
```

`zipformer_ctc`

```json
{
  "model": "model.onnx"
}
```

`canary`

```json
{
  "encoder": "encoder.onnx",
  "decoder": "decoder.onnx",
  "src_lang": "en",
  "tgt_lang": "en",
  "use_pnc": 1
}
```

`wenet_ctc`

```json
{
  "model": "model.onnx"
}
```

`omnilingual`

```json
{
  "model": "model.onnx"
}
```

`medasr`

```json
{
  "model": "model.onnx"
}
```

`funasr_nano`

```json
{
  "encoder_adaptor": "encoder_adaptor.onnx",
  "llm": "llm.onnx",
  "embedding": "embedding.onnx",
  "tokenizer": "tokenizer.model",
  "system_prompt": "",
  "user_prompt": "",
  "max_new_tokens": 0,
  "temperature": 0.0,
  "top_p": 0.0,
  "seed": 0,
  "language": "",
  "itn": 0,
  "hotwords": ""
}
```

`fire_red_asr_ctc`

```json
{
  "model": "model.onnx"
}
```

## Notes

- This draft is schema-oriented only. It does not define migration behavior.
- This draft assumes that users may edit any value in `vinput-model.json`.
- Future refinement should focus on:
  - which fields are actually mandatory per family
  - path resolution rules for relative paths
  - whether empty-string placeholders should remain explicit in shipped files
```
