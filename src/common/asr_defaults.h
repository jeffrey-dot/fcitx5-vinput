#pragma once

namespace vinput::asr {

inline constexpr char kBuiltinProviderType[] = "builtin";
inline constexpr char kCommandProviderType[] = "command";
inline constexpr char kDefaultProviderName[] = "local";
inline constexpr char kDefaultBuiltinModel[] = "paraformer-zh-small";
inline constexpr int kDefaultProviderTimeoutMs = 15000;

} // namespace vinput::asr
