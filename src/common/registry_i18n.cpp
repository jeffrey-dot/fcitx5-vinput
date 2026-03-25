#include "common/registry_i18n.h"

#include <curl/curl.h>

#include <cstdlib>
#include <nlohmann/json.hpp>

#include "common/core_config.h"

namespace vinput::registry {

namespace {

using json = nlohmann::json;

struct MemoryBuffer {
  std::string data;
  size_t max_size = 0;
};

size_t CurlMemoryWriteCallback(char *ptr, size_t size, size_t nmemb,
                               void *userdata) {
  auto *buf = static_cast<MemoryBuffer *>(userdata);
  const size_t total = size * nmemb;
  if (buf->max_size > 0 && buf->data.size() + total > buf->max_size) {
    return 0;
  }
  buf->data.append(ptr, total);
  return total;
}

std::string NormalizeLocale(std::string locale) {
  if (locale.empty()) {
    return "en_US";
  }
  const auto dot_pos = locale.find('.');
  if (dot_pos != std::string::npos) {
    locale = locale.substr(0, dot_pos);
  }
  const auto at_pos = locale.find('@');
  if (at_pos != std::string::npos) {
    locale = locale.substr(0, at_pos);
  }
  for (char &ch : locale) {
    if (ch == '-') {
      ch = '_';
    }
  }
  if (locale == "C" || locale == "POSIX") {
    return "en_US";
  }
  const auto sep = locale.find('_');
  if (sep == std::string::npos) {
    if (locale == "zh") {
      return "zh_CN";
    }
    if (locale == "en") {
      return "en_US";
    }
  }
  return locale;
}

I18nMap FetchI18nMapOnce(const std::string &url, std::string *error) {
  I18nMap map;

  CURL *curl = curl_easy_init();
  if (!curl) {
    if (error) {
      *error = "failed to initialize libcurl";
    }
    return map;
  }

  MemoryBuffer buf;
  buf.max_size = 1024 * 1024;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlMemoryWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

  CURLcode res = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    if (error) {
      *error = std::string("curl error: ") + curl_easy_strerror(res);
    }
    return map;
  }
  if (http_code != 200) {
    if (error) {
      *error = "i18n fetch failed with HTTP " + std::to_string(http_code);
    }
    return map;
  }

  try {
    json j = json::parse(buf.data);
    if (!j.is_object()) {
      if (error) {
        *error = "i18n JSON is not an object";
      }
      return map;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
      if (it.value().is_string()) {
        map[it.key()] = it.value().get<std::string>();
      }
    }
  } catch (const std::exception &ex) {
    if (error) {
      *error = std::string("failed to parse i18n JSON: ") + ex.what();
    }
    return {};
  }

  return map;
}

} // namespace

std::string DetectPreferredLocale() {
  const char *vars[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
  for (const char *name : vars) {
    const char *value = std::getenv(name);
    if (value && *value) {
      return NormalizeLocale(value);
    }
  }
  return "en_US";
}

I18nMap FetchI18nMap(const std::vector<std::string> &urls, std::string *error) {
  I18nMap map;
  std::string last_error;
  for (const auto &url : urls) {
    if (url.empty()) {
      continue;
    }
    map = FetchI18nMapOnce(url, &last_error);
    if (!map.empty()) {
      if (error) {
        error->clear();
      }
      return map;
    }
  }

  if (error) {
    *error = last_error.empty() ? "failed to fetch i18n from all sources"
                                : last_error;
  }
  return {};
}

I18nMap FetchMergedI18nMap(const CoreConfig &config,
                           const std::string &preferred_locale,
                           std::string *error) {
  I18nMap merged;
  std::string fetch_error;

  const auto primary_urls = ResolveRegistryI18nUrls(config, preferred_locale);
  if (!primary_urls.empty()) {
    merged = FetchI18nMap(primary_urls, &fetch_error);
  }

  if (merged.empty() && preferred_locale != "en_US") {
    const auto fallback_urls = ResolveRegistryI18nUrls(config, "en_US");
    auto fallback = FetchI18nMap(fallback_urls, &fetch_error);
    merged.insert(fallback.begin(), fallback.end());
  } else if (preferred_locale != "en_US") {
    const auto fallback_urls = ResolveRegistryI18nUrls(config, "en_US");
    auto fallback = FetchI18nMap(fallback_urls, nullptr);
    for (auto &[key, value] : fallback) {
      merged.emplace(std::move(key), std::move(value));
    }
  }

  if (error) {
    *error = fetch_error;
  }
  return merged;
}

std::string LookupI18n(const I18nMap &map, const std::string &key,
                       const std::string &fallback) {
  const auto it = map.find(key);
  if (it == map.end() || it->second.empty()) {
    return fallback;
  }
  return it->second;
}

} // namespace vinput::registry
