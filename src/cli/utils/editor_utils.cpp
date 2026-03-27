#include "cli/utils/editor_utils.h"

#include <cctype>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

bool ParseEditorCommand(const char* command, std::vector<std::string>* args) {
  if (!command || !args) {
    return false;
  }

  args->clear();
  std::string current;
  bool in_single_quote = false;
  bool in_double_quote = false;
  bool escaping = false;

  for (const char* p = command; *p != '\0'; ++p) {
    const char ch = *p;

    if (escaping) {
      current.push_back(ch);
      escaping = false;
      continue;
    }

    if (!in_single_quote && ch == '\\') {
      escaping = true;
      continue;
    }

    if (!in_double_quote && ch == '\'') {
      in_single_quote = !in_single_quote;
      continue;
    }

    if (!in_single_quote && ch == '"') {
      in_double_quote = !in_double_quote;
      continue;
    }

    if (!in_single_quote && !in_double_quote &&
        std::isspace(static_cast<unsigned char>(ch))) {
      if (!current.empty()) {
        args->push_back(current);
        current.clear();
      }
      continue;
    }

    current.push_back(ch);
  }

  if (escaping || in_single_quote || in_double_quote) {
    return false;
  }

  if (!current.empty()) {
    args->push_back(current);
  }

  return !args->empty();
}

}  // namespace

int OpenInEditor(const std::filesystem::path& file_path) {
  const char* editor = getenv("VISUAL");
  if (!editor || editor[0] == '\0') {
    editor = getenv("EDITOR");
  }
  if (!editor || editor[0] == '\0') {
    editor = "vi";
  }

  std::vector<std::string> command_args;
  if (!ParseEditorCommand(editor, &command_args)) {
    return -1;
  }

  pid_t pid = fork();
  if (pid < 0) {
    return -1;
  }
  if (pid == 0) {
    // Child
    command_args.push_back(file_path.string());
    std::vector<char*> argv;
    argv.reserve(command_args.size() + 1);
    for (auto& arg : command_args) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    execvp(command_args[0].c_str(), argv.data());
    // execvp failed
    _exit(127);
  }

  // Parent: wait for editor to exit
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    return -1;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return -1;
}
