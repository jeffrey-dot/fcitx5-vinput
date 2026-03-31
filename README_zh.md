<div align="center">

# fcitx5-vinput

**基于 Fcitx5 的本地离线语音输入插件**

[![License](https://img.shields.io/github/license/xifan2333/fcitx5-vinput)](LICENSE)
[![CI](https://github.com/xifan2333/fcitx5-vinput/actions/workflows/ci.yml/badge.svg)](https://github.com/xifan2333/fcitx5-vinput/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/xifan2333/fcitx5-vinput)](https://github.com/xifan2333/fcitx5-vinput/releases)
[![AUR](https://img.shields.io/aur/version/fcitx5-vinput-bin)](https://aur.archlinux.org/packages/fcitx5-vinput-bin)
[![Downloads](https://img.shields.io/github/downloads/xifan2333/fcitx5-vinput/total)](https://github.com/xifan2333/fcitx5-vinput/releases)

[English](README.md) | [中文](README_zh.md)

https://github.com/user-attachments/assets/5a548a68-153c-4842-bab6-926f30bb720e

</div>

基于 [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) 实现本地离线语音识别，支持通过任意 OpenAI 兼容 API 进行 LLM 后处理。

## 功能特性

- **两种触发模式** — 短按切换录音开关，长按即说即停（push-to-talk）
- **命令模式** — 选中文本，语音指令直接修改
- **LLM 后处理** — 纠错、格式化、翻译等
- **场景管理** — 运行时随时切换后处理 prompt
- **多 LLM provider** — 配置多个服务端，并按场景绑定
- **热词支持**（部分模型）
- **`vinput` CLI** — 在终端管理模型、场景、provider 和 daemon 状态

## 安装

### Arch Linux (AUR)

```bash
yay -S fcitx5-vinput-bin
```

### Fedora (COPR)

```bash
sudo dnf copr enable xifan/fcitx5-vinput-bin
sudo dnf install fcitx5-vinput
```

### Ubuntu 24.04 (PPA)

```bash
sudo add-apt-repository ppa:xifan233/ppa
sudo apt update
sudo apt install fcitx5-vinput
```

### Ubuntu / Debian（手动安装）

```bash
# 从 GitHub Releases 下载最新 .deb
sudo dpkg -i fcitx5-vinput_*.deb
sudo apt-get install -f
```

### Nix（通过 flake）

- 当前支持 `x86_64-linux` 和 `aarch64-linux`

#### Home Manager 使用示例

- 将 `fcitx5-vinput` 添加为你的 flake 输入：

```nix
{
  description = "Your flake description";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
    home-manager.url = "github:nix-community/home-manager";
    home-manager.inputs.nixpkgs.follows = "nixpkgs";

    fcitx5-vinput = {
      url = "github:xifan2333/fcitx5-vinput";
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      nixos-hardware,
      home-manager,
      ...
    }@inputs:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
      };
      homeManagerConfiguration = home-manager.lib.homeManagerConfiguration;
    in
    {
      homeConfigurations = {
        "kakapt@krypton" = homeManagerConfiguration {
          inherit pkgs;
          modules = [ ./your_home_module.nix ];
          extraSpecialArgs = inputs;
        };
      };
    };
}
```

- 然后将 `fcitx5-vinput` 放入你的 `fcitx5` 插件包装中：

```nix
{ pkgs, ... }@inputs:
let
  fcitx5-vinput = inputs.fcitx5-vinput.packages."${pkgs.stdenv.hostPlatform.system}".default;
in
{
  home.packages = [
    fcitx5-vinput
  ];

  i18n.inputMethod = {
    enable = true;
    type = "fcitx5";
    fcitx5.addons = with pkgs; [
      fcitx5-vinput
    ];
  };
}
```

### 从源码编译

**依赖：** `cmake` `fcitx5` `pipewire` `libcurl` `nlohmann-json` `CLI11` `Qt6`

```bash
sudo bash scripts/build-sherpa-onnx.sh
cmake --preset release-clang-mold
cmake --build --preset release-clang-mold
sudo cmake --install build
```

如果你使用 `just`，仓库里现在也提供了一层很薄的命令包装，底层仍然是同一套
CMake / 脚本入口：

```bash
just sherpa
just release
just build
sudo just install
```

第一步会下载本地构建和发布打包所用的预编译 `sherpa-onnx` 运行库。
这些运行库会随安装产物一起打包，而不是作为单独的系统依赖项声明。
从源码构建时现在默认安装到 Fcitx5 的系统前缀（`/usr`），这样 addon
会落到 Fcitx5 会扫描的目录里。若你之前的旧构建已经安装到 `/usr/local`，
请清理旧 build 目录后重新安装，让 `vinput.conf` 和 `fcitx5-vinput.so`
进入 Fcitx5 的系统路径。

## 快速开始

**1. 安装模型**

```bash
vinput model list -a            # 浏览可用模型
vinput model add <模型名>        # 下载并安装
vinput model use <模型名>        # 设置为当前模型
```

也可手动将模型目录放到 `~/.local/share/vinput/models/<模型名>/`，目录内需包含：
- `vinput-model.json`
- `model.int8.onnx` 或 `model.onnx`
- `tokens.txt`

**2. 启动守护进程**

```bash
systemctl --user enable --now vinput-daemon.service
```

如果你之前把 Vinput 安装在 `/usr/local`，请先删除那里的旧用户态
service 文件，并执行 `systemctl --user daemon-reload`，否则旧 unit
可能会覆盖新的 `/usr` 安装结果。

**3. 在 Fcitx5 中启用**

打开 Fcitx5 配置 → 附加组件 → 找到 **Vinput** → 启用。

**4. 开始使用**

- **短按** `Alt_R` 开始录音，再按一次停止并识别
- **长按** `Alt_R` 录音，松开自动识别上屏（push-to-talk）

## 按键说明

| 按键 | 默认 | 功能 |
|------|------|------|
| 触发键 | `Alt_R` | 短按切换录音；长按即说即停 |
| 命令键 | `Control_R` | 选中文本后按住，语音指令修改选中内容 |
| ASR 菜单键 | `F8` | 打开 ASR provider / 模型切换菜单 |
| 场景菜单键 | `Shift_R` | 打开场景切换菜单 |
| 翻页 | `Page Up` / `Page Down` | 候选列表翻页 |
| 移动 | `↑` / `↓` | 候选列表移动光标 |
| 确认 | `Enter` | 确认选中候选 |
| 取消 | `Esc` | 关闭菜单 |
| 快选 | `1`–`9` | 快速选择候选 |

所有按键均可在 Fcitx5 配置界面中自定义。

## 配置

### 图形界面

```bash
vinput-gui
```

或在 Fcitx5 配置中打开 Vinput 附加组件。

### CLI 参考

完整当前语法可使用 `vinput --help` 或 `vinput <子命令> --help` 查看。

> 如果要在 Flatpak 安装下使用 Vinput CLI，请使用 `flatpak run --command=/app/addons/Vinput/bin/vinput org.fcitx.Fcitx5 --help`

<details>
<summary>模型管理</summary>

```bash
vinput model list               # 列出已安装模型
vinput model list -a            # 列出可用远程模型
vinput model add <名称>          # 下载安装模型
vinput model use <名称>          # 切换当前模型
vinput model remove <名称>       # 删除模型
vinput model info <名称>         # 查看模型详情
```

</details>

<details>
<summary>场景管理</summary>

```bash
vinput scene list               # 列出所有场景
vinput scene add --id <id>      # 添加场景
vinput scene edit <id>          # 编辑场景
vinput scene use <id>           # 切换当前场景
vinput scene remove <id>        # 删除场景
```

如果场景要调用 LLM，`--provider`、`--model`、`--prompt` 需要同时提供。

</details>

<details>
<summary>LLM 配置</summary>

```bash
vinput llm list                         # 列出已配置 LLM provider
vinput llm add <id> --base-url <url>    # 添加 LLM provider
vinput llm edit <id> --base-url <url>   # 编辑 LLM provider
vinput llm remove <id>                  # 删除 LLM provider
vinput adapter list                     # 列出已安装 LLM adapter
vinput adapter list -a                  # 列出可用远程 adapter
vinput adapter add <id>                 # 从注册源安装 adapter
vinput adapter start <id>               # 启动 adapter
vinput adapter stop <id>                # 停止 adapter
```

LLM provider 由场景引用，不再有单独的“当前 provider”开关。LLM adapter
只是本地 OpenAI 兼容桥接进程，provider 可以指向它。

</details>

<details>
<summary>ASR Provider</summary>

```bash
vinput provider list            # 列出已配置 ASR provider
vinput provider list -a        # 列出可用远程 ASR provider
vinput provider add <id>       # 从注册源安装 provider
vinput provider use <id>       # 切换当前 ASR provider
vinput provider edit <id>      # 编辑外部 provider 脚本
vinput provider remove <id>    # 删除 provider
```

</details>

<details>
<summary>热词管理</summary>

```bash
vinput hotword get              # 查看当前热词文件路径
vinput hotword set <路径>        # 设置热词文件
vinput hotword edit             # 用编辑器打开热词文件
vinput hotword clear            # 清除热词文件配置
```

</details>

<details>
<summary>设备管理</summary>

```bash
vinput device list              # 列出录音设备
vinput device use <名称>         # 设置当前设备
```

</details>

<details>
<summary>配置辅助</summary>

```bash
vinput config get /global/capture_device        # 读取配置项（JSON Pointer）
vinput config set /global/capture_device <值>   # 写入配置项
vinput config edit core                         # 编辑核心配置（config.json）
vinput config edit fcitx                        # 编辑 Fcitx 插件配置（vinput.conf）
```

高级注册源回退可直接在 `config.json` 里配置分类型的 source 列表，例如
`registry.models`、`registry.asr_providers`、`registry.llm_adapters`、
`registry.i18n`。程序会按顺序依次尝试这些 URL，直到某个源成功。

</details>

<details>
<summary>录音控制</summary>

```bash
vinput recording start              # 开始录音
vinput recording stop               # 停止录音并识别
vinput recording stop --scene <ID>  # 使用指定场景停止录音
vinput recording toggle             # 切换录音开始/停止
vinput recording toggle --scene <ID># 使用指定场景切换录音
```

</details>

<details>
<summary>守护进程管理</summary>

```bash
vinput daemon start             # 启动
vinput daemon stop              # 停止
vinput daemon restart           # 重启
vinput daemon log               # 查看日志
vinput daemon log -f            # 跟踪日志
vinput init                     # 创建默认配置和模型目录
```

</details>

## 场景

场景控制 LLM 对识别结果的处理方式，通过场景菜单键在运行时切换。

每个场景包含：
- **ID** — 唯一标识符
- **标签** — 菜单中显示的名称
- **Prompt** — 发送给 LLM 的系统提示

只有同时配置了 `provider + model + prompt` 的场景才会调用 LLM。
内置的 `__raw__` 场景会完全跳过 LLM。

## 命令模式

选中文本 → 按住命令键 → 说出指令 → 松开 → 完成。

如果当前没有可用的 surrounding-text 选区，命令模式会回退到当前的
primary selection 剪贴板内容。这样操作上更顺手，但也意味着如果 primary
selection 里残留的是旧文本或无关文本，它也可能被送入命令改写流程。

**示例：**
- 选中中文 → 说 *"翻译成英文"* → 替换为英文译文
- 选中代码 → 说 *"加上注释"* → 替换为加注释版本

> 需要先配置并启用 LLM。

## LLM 配置示例

以本地 [Ollama](https://ollama.com) 为例：

```bash
vinput llm add ollama --base-url http://127.0.0.1:11434/v1
vinput scene add --id polish \
  --label "Polish" \
  --provider ollama \
  --model qwen2.5:7b \
  --prompt "将识别结果润色为自然的中文。"
vinput scene use polish
```

## Provider 脚本与 Adapter 约定

可选集成脚本由 `vinput-registry` 发布，通过 CLI 按需安装到 `~/.config/vinput/`。

`scripts/` 目录只保留构建、检查、打包之类的项目维护脚本。

### ASR command provider 协议

外部 ASR 脚本应遵循传统 Unix 命令语义：

- `stdin`：一段完整语音的原始音频流
- `stdout`：最终识别文本
- `stderr`：可读的错误信息
- 退出码 `0`：成功
- 非 `0` 退出码：失败

当前 `vinput` 传给 command provider 的音频格式是：

- PCM `S16_LE`
- 单声道
- `16000 Hz`

也就是脚本收到的是裸 PCM 字节流，需要脚本自己决定是否封装成 WAV 或转发到云接口。

一个最小的自定义 provider 配置示例：

```json
{
  "name": "openai-compatible",
  "type": "command",
  "command": "python3",
  "args": [
    "~/.config/vinput/asr-providers/openai-compatible.py"
  ],
  "env": {
    "VINPUT_ASR_API_KEY": "...",
    "VINPUT_ASR_URL": "https://api.openai.com/v1/audio/transcriptions",
    "VINPUT_ASR_MODEL": "gpt-4o-mini-transcribe"
  },
  "timeout_ms": 60000
}
```

官方云端 ASR provider 脚本现在由 `vinput-registry` 发布，并通过
`vinput provider add <id>` 按需安装到 `~/.local/share/vinput/providers/`。
`command` 应该填写可执行命令或解释器，脚本路径放在 `args` 里。

当前官方云端 ASR provider 包括：

- `elevenlabs`
- `openai-compatible`
- `doubao`

其中 `openai-compatible` 覆盖 OpenAI 官方、SiliconFlow 以及其他
OpenAI 兼容转写接口，只需要调整 URL 和 model 环境变量；`doubao`
直接对接豆包语音 / 火山引擎录音文件极速版接口。

### Streaming ASR provider 协议草案

对于流式 provider，建议由 `vinput-registry` 定义一套统一的事件协议，
要求所有 provider 脚本先把上游厂商的事件格式适配成这套协议，再交给
主程序消费。主程序只承接这套统一协议，不再猜测某个 provider 返回的
到底是“增量文本”还是“全文文本”。

建议统一语义如下：

- `partial.text`：当前时刻用户应该看到的完整文本
- `final.text`：当前时刻已经确认的完整文本
- `segment_final`：可选布尔值，表示一个 segment 已完成
- `utterance_final`：可选布尔值，表示整次语音输入已经结束
- `words`：可选逐词时间戳信息

建议规则如下：

- 文本累计、去重、分段拼接属于 provider 脚本职责。
- 如果上游 API 只返回当前 segment 的增量文本，脚本必须先累计，再输出
  `partial.text` 或 `final.text`。
- 如果上游 API 已经返回全文，脚本可以直接透传。
- `vinput` 主程序只把 `partial.text` 当作 preedit，把最后一个
  `final.text` 当作识别结果。
- `vinput` 主程序不再为某一家云 ASR 单独实现拼接逻辑。

为什么不直接照搬 OpenAI Realtime：

- OpenAI Realtime 很适合作为参考，但不同厂商的事件语义并不一致。
- 即使是 OpenAI 风格的 `delta`，在不同模型或不同服务里也未必都严格表示
  同一种增量语义。
- 内部定义一套更严格的协议，更利于文档约束、脚本实现和主程序消费。

建议 JSONL 输出示例：

```json
{"type":"partial","text":"hello world"}
{"type":"final","text":"hello world","segment_final":true}
{"type":"partial","text":"hello world again"}
{"type":"final","text":"hello world again","segment_final":true,"utterance_final":true}
```

### LLM Adapter 协议

如果你要自己写 LLM adapter，它需要提供 OpenAI 兼容接口，至少实现：

- `GET /v1/models`
- `POST /v1/chat/completions`

`vinput` 当前对 `POST /v1/chat/completions` 的要求是：

- 非流式：`"stream": false`
- 返回标准的 OpenAI chat completion JSON
- `choices[0].message.content` 必须是一个字符串
- 这个字符串本身必须是 JSON，格式如下：

```json
{
  "candidates": [
    "候选结果 1",
    "候选结果 2"
  ]
}
```

也就是说，外层是 OpenAI 兼容响应，内层 `content` 是 `vinput` 目前消费的结构化 JSON 字符串。

`GET /v1/models` 只需要返回 OpenAI 常见格式即可，例如：

```json
{
  "object": "list",
  "data": [
    {
      "id": "my-proxy-model",
      "object": "model",
      "owned_by": "my-proxy"
    }
  ]
}
```

官方 LLM adapter 现在由 `vinput-registry` 发布，并通过
`vinput adapter add <id>` 按需安装到 `~/.local/share/vinput/adapters/`。

对于托管的 LLM adapter，运行时配置建议统一走环境变量，不要依赖 CLI
位置参数。`vinput adapter start/stop` 直接启动脚本，不会为它注入额外位置参数。

参考实现见 `vinput-registry` 仓库。

## 配置文件位置

| 文件 | 路径 |
|------|------|
| Fcitx5 插件配置（按键等） | `~/.config/fcitx5/conf/vinput.conf` |
| 核心配置（模型、LLM、场景） | `~/.config/vinput/config.json` |
| 模型目录 | `~/.local/share/vinput/models/` |

## Flatpak

Flatpak 下，Vinput 是作为 Fcitx5 Add-on 的形式安装的

### 额外的权限

Vinput Add-on 的权限取决于 Fcitx5 本体，因此安装后需要执行以下命令，来为 Fcitx5 提供权限

```bash
# 提供基于 pipewire 的麦克风访问
flatpak override --user --filesystem=xdg-run/pipewire-0 org.fcitx.Fcitx5
# 让 vinput-daemon.service 能被创建
flatpak override --user --filesystem=xdg-config/systemd:create org.fcitx.Fcitx5
```

执行后，必须重启 Fcitx5。可以：

```bash
flatpak kill org.fcitx.Fcitx5
```

### Flatpak 配置文件位置

Flatpak 下配置文件通常在 `.var/app` 中

| 文件 | 路径 |
|------|------|
| Fcitx5 插件配置（按键等） | `~/.var/app/org.fcitx.Fcitx5/config/fcitx5/conf/vinput.conf` |
| 核心配置（模型、LLM、场景） | `~/.var/app/org.fcitx.Fcitx5/config/vinput/config.json` |
| 模型目录 | `~/.var/app/org.fcitx.Fcitx5/data/vinput/models/` |

## 打包发布

推送形如 `v0.1.0` 的 tag 后，GitHub Actions 会自动构建并发布：

- 源码包 `fcitx5-vinput-<version>.tar.gz`
- Ubuntu 24.04 `.deb`
- Debian 12 `.deb`
- Arch Linux `.pkg.tar.zst`
- Fedora COPR (`xifan/fcitx5-vinput-bin`)
- Ubuntu PPA (`ppa:xifan233/ppa`)
