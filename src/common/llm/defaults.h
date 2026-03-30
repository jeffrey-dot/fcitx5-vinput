#pragma once

namespace vinput::llm {

inline constexpr char kAuthorizationHeader[] = "Authorization";
inline constexpr char kBearerPrefix[] = "Bearer ";
inline constexpr char kJsonContentTypeHeader[] =
    "Content-Type: application/json";
inline constexpr char kOpenAiChatCompletionsPath[] = "/chat/completions";
inline constexpr char kOpenAiModelsPath[] = "/models";
inline constexpr char kHttpUserAgent[] = "fcitx5-vinput/0.1";
inline constexpr int kModelFetchTimeoutMs = 5000;

} // namespace vinput::llm
