# General Harness 发行版

**核心引擎 + 双模前端**架构：后端统一封装云端 API 代理、Ollama 协议兼容与
GGUF 文件解析能力，暴露标准 HTTP API；前端提供两种交互模式——纯终端 CLI
（Go 内嵌，ANSI 动态思考展示）与 Webview GUI（Python，复用系统原生渲染引擎）。

## 架构

```
release/
├── bin/gh.exe        Go 引擎 + CLI（单二进制，零第三方依赖）
├── engine/           Go 引擎源码（标准库 net 手写 HTTP 层，无第三方依赖）
├── cli/              终端模式入口（内嵌于 gh.exe，目录保留扩展位）
├── gui/gui.py        Webview GUI（pywebview → Edge WebView2；不可用时回退浏览器）
├── models/           本地 GGUF 模型目录（*.gguf，仅解析能力）
├── plugins/          云端厂商配置（config.yaml 格式，与 Python 版兼容）
├── config.yaml       全局配置（server / gateway_api_key / rate_limit / aliases）
├── start.py          统一启动器
└── tests/            测试
```

## 快速开始

```bash
# 终端 CLI 模式（只启动后端 → CLI 终端）
python start.py

# Webview GUI 模式（引擎 + 图形界面，Thinking 可折叠）
python start.py --gui

# 仅引擎服务（供 Open WebUI 等 Ollama 生态工具对接）
python start.py --server
```

引擎 API 地址默认 `http://127.0.0.1:8000`，可用 `--port` 覆盖。

## 标准 HTTP API

| 端点 | 说明 |
| --- | --- |
| `GET /health` | 健康检查 |
| `POST /v1/chat/completions` | 统一对话（OpenAI 风格，支持 stream/非 stream，含 thinking） |
| `GET /v1/models` | 模型列表（云端 + 本地 GGUF） |
| `GET /v1/usage/stats` | 用量统计（时间维度 `time_range` / 对话维度 `limit`） |
| `GET /api/tags` | Ollama 兼容：模型列表 |
| `POST /api/chat` | Ollama 兼容：messages 对话（stream/非 stream） |
| `POST /api/generate` | Ollama 兼容：prompt 生成 |
| `GET /api/gguf/<name>` | 本地 GGUF 模型元数据 |

## 构建

```bash
# 引擎（需要 Go 1.21+；编译为单二进制）
cd engine && go build -ldflags "-s -w -buildid=" -trimpath -o ../bin/gh.exe .
# 可选：UPX 极限压缩（约 3 倍缩小）
upx --lzma -9 -o ../bin/gh_upx.exe ../bin/gh.exe
```

体积说明：Go 运行时在 Windows 上的物理下限约为 1.2MB（strip 符号），
含网络能力（`net` 包）后 strip 约 2.2MB——`<250KB` 仅无网络 hello-world
可达（0.44MB）。本项目采用"仅 `net` 包手写 HTTP 层 + 零第三方依赖"的
极致轻量路径：**strip 后 3.3MB，UPX 压缩后约 1.1MB**（接近含网络二进制的
理论下限 0.72MB）。引擎单文件、零外部依赖、解压即用。

## 测试

```bash
cd engine && go test ./...
python -m unittest discover -s tests
```
