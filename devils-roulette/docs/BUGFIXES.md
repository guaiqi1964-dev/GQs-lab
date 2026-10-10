# 原程序 bug 分析与修复报告

本文档记录从 `恶魔轮盘赌.exe` 还原出的源码中发现的 **4 个 bug**，
以及优化修复版（`恶魔轮盘赌_优化版.cpp`）的处理方式。

> 说明：前 3 个是**原程序真实存在的 bug**（有反汇编与二进制证据）；
> 第 4 个是**还原过程中我自己引入的偏差**，一并修正。

---

## BUG #1 —— 单人模式里恶魔永远不会行动 ★最严重

**现象**：单人模式下只有你一直在开枪，恶魔从头到尾不还手。玩家永远不可能输。

**证据**（反汇编，`.pdata` 异常表给出的精确函数边界）：

主循环 `0x402EC3` 的逻辑是：

```
0x402EC3  loop:
0x402ECB    eax = [someFlag]
0x402ED1    test eax,eax
0x402ED3    jne  0x402EE6
0x402ED5      call 0x40157E        ; build_gun_duo
0x402EDA      [someFlag] = 0
0x402EE4      jmp  0x402EFF
0x402EE6    eax = [turnCounter]    ; ← 只有这条路径才会走恶魔回合
0x402EEC    and eax, 1
0x402EF1    jne  0x402EFA
0x402EF3      call 0x401967        ; danrenplay（玩家回合）
0x402EF8      jmp  0x402EFF
0x402EFA      call 0x4028C3        ; demonTurn（恶魔回合）
0x402EFF    jmp  0x402ECB         ; 无限循环，永不返回
```

编译期常量是 `[someFlag] == 0`，所以第一条分支恒定成立：
**每一轮都只是把 `[someFlag]` 置 0，然后回到循环头，
`and eax,1` 这条决定"玩家还是恶魔"的分支永远到不了。**

结果：
- `danrenplay`（玩家回合）与 `demonTurn`（恶魔回合）**永远不会被主循环调用**；
- 恶魔回合的三段代码（`0x2478` / `0x26E5` / `0x28C3`，共 2635 字节）
  变成了死代码——它们引用的
  `"现在是恶魔的回合"`、`"恶魔决定向你开枪"`、
  `"恶魔决定向自己开枪"`、`"因为恶魔使用了手铐…"`、
  `"恶魔使用了小刀"`、`"恶魔看到了"`、`"有趣"` 等文案全都打不出来。

**修复**：优化版补上了正确的回合交替：

```cpp
for (;;) {
    if (g.queued <= 0) newRound(...);
    printBoard(...);
    while (!playerStep()) { }      // 玩家回合（道具/手铐可多次行动）
    demonTurn();                   // ← 原来缺失的一步
    gameOverSingle();
}
```

---

## BUG #2 —— 第五种道具「饮料」永远抽不到

**证据**（`build_gun_duo` @ `0x40157E` 的道具发放循环）：

```
0x4016EF  mov edx, 0x4          ; hi = 4
0x4016F4  mov ecx, 0x0          ; lo = 0
0x4016F9  call 0x401530         ; randomInt(0, 4)
0x4016FE  mov [rbp-32], eax     ; d = 结果
0x40170E  lea rax, [rip+715179] ; &daoju[]
0x401715  mov eax, [rdx+rax]    ; daoju[d]
0x401718  lea ecx, [rax+1]
0x40172F  mov [rdx+rax], ecx    ; daoju[d]++
```

`0x401530` 是 `randomInt(lo, hi)`（`IsOver_duo` 会连着调它两次取随机数）。
因为 `lo = 0`、`hi = 4`，取到的是 **0..3**，
而道具数组有 **5** 个槽位（放大镜/手铐/小刀/烟/饮料），
**下标 4 的「饮料」永远抽不到**。

**修复**：改为覆盖全部 5 种：

```cpp
p.items[rnd(0, ITEM_COUNT - 1)]++;   // ITEM_COUNT == 5
```

---

## BUG #3 —— 「获得了 N 个道具」显示的其实是上限

**证据**（同一函数）：

```
0x401693  mov eax, [rip+715287]   ; 已有道具总数
0x401699  mov edx, 0x8            ; 上限 8
0x40169E  sub edx, eax            ; 8 - 已有
0x4016A2  mov [rbp-52], eax
0x4016A5  mov [rbp-48], 0x4       ; 4
0x4016B7  call 0x474220           ; std::min(8-已有, 4)
0x4016BC  mov eax, [rax]
0x4016BE  mov [rbp-28], eax       ; got = min(...)
0x4016C6  lea rcx, "你获得了%d个道具:\n"
0x4016CD  call printf             ; 打印的是 got
...
0x4016E3  ; 之后固定循环 4 次真正发道具
```

所以屏幕上的数字是 **`min(8 - 已有, 4)`（还能再带几件）**，
而每回合真正发放的是**固定 4 件**。开局背包为空时会显示
「你获得了8个道具」，实际只进 4 件。
（顺带解释了规则文本里"获得4个道具作为补给（上限为8个）"的说法。）

**修复**：如实打印本回合真正发出的件数。优化版还顺手加了到上限即停：

```cpp
static int supplyItems(Player& p) {
    int given = 0;
    for (int i = 0; i < rules::kSupply; ++i) {
        if (p.itemTotal() >= rules::kItemCap) break;
        ++p.items[rnd(0, ITEM_COUNT - 1)];
        ++given;
    }
    return given;
}
```

---

## BUG #4 —— 还原版把「恶魔生命」写成了「恶魔的生命」

这是**我在还原时引入的偏差**，不是原程序的 bug。

原程序的格式串是 `恶魔生命:%d/6`（无「的」），
而双人模式用的是 `玩家A的生命:%d/4`（有「的」）。
我一开始统一成 `%s的生命` 模板，导致单人模式的输出多了一个「的」。

**修复**：按原文案分别写死两套格式串。

---

## BUG #5 —— 清屏之前没有刷新自己的输出缓冲

**发现途径**：做「查看道具效果」功能时，自动化测试突然只收到 `0x0C`
（`cls` 的控制字符），一个字的中文都看不到。排查后确认这是一个**独立存在**的输出 bug。

```cpp
// 错误写法
static void clearScreen() { std::system("cls"); }
```

`system()` 只会刷新 **C 运行库自己缓冲区**里的东西；我们自己的 `printf`
文本还留在 stdio 缓冲里没送出去，紧接着的 `cls` 就把这部分内容丢掉了。

- 在**真实控制台**里通常看不出来（终端多为行缓冲，遇到换行就刷新了）；
- 一旦**重定向输出**（管道 / 写文件 / 自动化测试），就会变成满屏只有 `0x0C`。

**修复**：清屏和暂停之前先刷新标准输出。

```cpp
static void clearScreen() {
    std::fflush(stdout);
    std::system("cls");
}
static void pauseScreen() {
    std::fflush(stdout);
    std::system("pause");
}
```

**实测**：修复前同一次会话只收到 4634 字节纯 `0x0C`；
修复后拿到完整的正常文本。忠实还原版也同步修复。


---

## 另外修正的实现问题（不算原程序 bug，但影响可维护性）

| 问题 | 原程序 | 优化版 |
|---|---|---|
| 道具分支重复 | 「你/恶魔/玩家A/玩家B」四套几乎相同的分支 | 统一 `useItem(Player&, int)` |
| 输入校验重复 | 每处各写一遍 `scanf` + 范围判断 | 统一 `askNumber(lo, hi)`，并吃掉残留字符防死循环 |
| 模式输入未校验 | 输入非 1/2 时不提示，直接落到双人模式 | 与其它菜单一致：提示「输入不合法」并重问 |
| 全局状态 | 一堆全局布尔量（`kao_you`/`kaoemo`/`asdasd`/`KnowItem`）双方共用 | 收进 `Player` 结构体，双方互不干扰 |
| 死函数 | `IsOver_duo` 定义但从未调用（g++ 会警告） | 移除，双人胜负直接判断 |
| 魔法数字 | `1`、`4`、`8`、`6` 散落在各处 | 集中到 `namespace rules` |
| 道具名称与说明 | 名字数组 + 规则文本各写一份 | 合成 `kItems[]` 表，一处维护 |
| `wait()` | 调用 Windows `Sleep()`，不可移植 | 用 `<ctime>` 忙等实现，跨平台可编译 |

---

## 编译与验证

```bash
g++ -std=c++17 -O2 -Wall -Wextra \
    -finput-charset=UTF-8 -fexec-charset=GBK \
    -o 恶魔轮盘赌.exe 恶魔轮盘赌_优化版.cpp
```

> `-fexec-charset=GBK` **必须加**。新版 GCC 默认把窄字符串按 UTF-8 编码，
> 而原程序是 GBK(CP936)，不加这个参数中文在 Windows 控制台会是乱码。

已在本机（MinGW-w64 GCC 16.2.0）实测：
- 编译零警告零错误；
- 实际运行输出包含原程序全部 144 条已核实文案；
- 单人模式下恶魔会正常还手（BUG #1 已修复）；
- 道具能抽到「饮料」（BUG #2 已修复）。
