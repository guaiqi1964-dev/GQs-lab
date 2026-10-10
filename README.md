# GQ's lab

Welcome to **GQ's lab** — 一个放"不知道该传哪个仓库"和"体量还不足以单独开仓库"
的东西的公共工作区。

## 目录分类

| 目录 | 放什么 | 一句话判断 |
|---|---|---|
| [`00_INBOX/`](./00_INBOX/) | 临时收件箱 | **还没想好放哪**就先扔这里（记得清） |
| [`10_reverse-engineering/`](./10_reverse-engineering/) | 逆向与分析 | 我在**拆别人的东西** |
| [`20_projects/`](./20_projects/) | 完整项目 | **有名字、要长期维护**的 |
| [`30_experiments/`](./30_experiments/) | 实验试水 | 为了**搞明白一件事**写的 |
| [`40_notes/`](./40_notes/) | 笔记与踩坑 | **我自己写的文字** |
| [`50_reference/`](./50_reference/) | 参考资料 | **从别处收集**、将来要查的 |
| [`99_misc/`](./99_misc/) | 杂项兜底 | 以上都不沾，但**舍不得删** |

每个目录里都有一个 `README.md`，写明"归这里 / 不归这里"，上传前扫一眼即可。

## 已有的东西

- **[general-harness/](./general-harness/)** — General Harness：极简 AI 网关
  （Go 单二进制引擎 + 终端 CLI + Webview GUI，支持云端代理 / Ollama 协议 /
  GGUF 解析 / 用量统计）。详见 [产品介绍与快速开始](./general-harness/README.md)。

- **[devils-roulette/](./devils-roulette/)** — 恶魔轮盘赌 · 逆向还原与优化：
  从一个只剩 EXE、源码已丢失的控制台小游戏里还原出完整 C++ 源码
  （借助 COFF 符号表 + `.pdata` 异常表 + 自研 x86-64 反汇编器），
  修掉 5 个 bug，重写恶魔 AI 为胜率搜索，并增加难度选项与体验改进。
  详见 [项目说明](./devils-roulette/README.md)。

> 这两个都诞生在分类目录建立之前，暂时留在根目录。
> 想归位的话：`general-harness/` 适合 `20_projects/`，
> `devils-roulette/` 适合 `10_reverse-engineering/`（或 `20_projects/`）。
> 移动会改变所有链接，所以没有替你做决定。

## 使用约定

1. **想 3 秒还不知道放哪 → 放 `00_INBOX/`**，别在根目录乱堆。
2. **收件箱不要积压** —— 超过 3 项或躺了 1 个月就归类掉。
3. **一个项目一个目录，目录里自带 `README.md`**，说明它是干什么的。
4. **大文件不进 Git**（安装包 / 模型权重 / 数据集），改用 Release 附件或网盘。
5. 项目大到值得单开仓库时，就**搬出去**，在这里留一行链接。

## About

This repository serves as GQ's laboratory for ideas, prototypes, and shared knowledge.

## License

All rights reserved unless otherwise noted.
