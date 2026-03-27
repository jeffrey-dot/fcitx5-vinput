#include "common/registry/registry_cache.h"

#include "common/utils/file_utils.h"
#include "common/utils/path_utils.h"

namespace vinput::registry::cache {

namespace {

bool LoadCachedText(const std::filesystem::path &cache_path, std::string *content,
                    vinput::download::Result *result, std::string *error) {
  std::string read_error;
  if (!vinput::file::ReadTextFile(cache_path, content, &read_error)) {
    if (error) {
      *error = read_error;
    }
    return false;
  }

  if (result) {
    result->ok = true;
    result->resolved_url = cache_path.string();
    result->http_code = 0;
    result->bytes_received = content ? content->size() : 0;
    result->error.clear();
  }
  if (error) {
    error->clear();
  }
  return true;
}

bool SaveCachedText(const std::filesystem::path &cache_path,
                    const std::string &content, std::string *error) {
  return vinput::file::AtomicWriteTextFile(cache_path, content, error);
}

std::filesystem::path CachePath(std::string_view name) {
  return vinput::path::RegistryCacheDir() / name;
}

}

bool FetchText(const std::vector<std::string> &urls,
               const std::filesystem::path &cache_path,
               const vinput::download::Options &options, std::string *content,
               vinput::download::Result *result, std::string *error) {
  if (content) {
    content->clear();
  }

  vinput::download::Result download_result;
  if (vinput::download::DownloadText(urls, options, content, &download_result)) {
    std::string write_error;
    if (!SaveCachedText(cache_path, content ? *content : std::string{},
                        &write_error)) {
      if (error) {
        *error = write_error;
      }
      if (result) {
        *result = std::move(download_result);
      }
      return false;
    }
    if (result) {
      *result = std::move(download_result);
    }
    if (error) {
      error->clear();
    }
    return true;
  }

  std::string cache_error;
  if (LoadCachedText(cache_path, content, result, &cache_error)) {
    if (error) {
      error->clear();
    }
    return true;
  }

  if (result) {
    *result = std::move(download_result);
  }
  if (error) {
    if (!download_result.error.empty()) {
      *error = download_result.error + "; cache unavailable: " + cache_error;
    } else {
      *error = cache_error;
    }
  }
  return false;
}

std::filesystem::path ModelRegistryPath() {
  return CachePath("models.json");
}

std::filesystem::path AsrProviderRegistryPath() {
  return CachePath("asr-providers.json");
}

std::filesystem::path LlmAdaptorRegistryPath() {
  return CachePath("llm-adaptors.json");
}

std::filesystem::path I18nPath(const std::string &locale) {
  return CachePath(std::string("i18n/") + locale + ".json");
}

} // namespace vinput::registry::cache
