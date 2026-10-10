# 战斗中查看道具效果（第 8 号选项）

## 需求

玩着玩着可能会忘记某种道具是干什么用的，所以行动菜单增加一个选项，
随时可以把五种道具的效果再看一遍；**看完不能跳回合，仍然轮到当前玩家行动。**

## 实现

### 菜单编号集中成常量

以前菜单编号是散落的魔法数字（`i + 3`、`choice >= 3`、`askNumber(1, 7)`），
加一个选项要改好几处。现在统一到一组常量：

```cpp
constexpr int kMenuShootFoe  = 1;                                  // 向对方开枪
constexpr int kMenuShootSelf = 2;                                  // 向自己开枪
constexpr int kMenuItemFirst = 3;                                  // 3..7 用五种道具
constexpr int kMenuItemLast  = kMenuItemFirst + ITEM_COUNT - 1;    // = 7
constexpr int kMenuItemHelp  = kMenuItemLast + 1;                  // = 8 查看道具效果
constexpr int kMenuLast      = kMenuItemHelp;                      // 合法输入上界
```

这样以后再加选项只要动这几个常量，判断逻辑不需要重复修改。

### 菜单与效果页

```cpp
static void printActionMenu(const Player& self, const char* labelFoe) {
    std::printf("%s要\n1.向%s开枪\n2.向自己开枪\n", self.name, labelFoe);
    for (int i = 0; i < ITEM_COUNT; ++i)
        std::printf("%d.使用%s\n", kMenuItemFirst + i, kItems[i].name);
    std::printf("%d.查看道具效果\n", kMenuItemHelp);      // 新增
}

// 只读：不消耗行动、不消耗道具、不结束回合
static void showItemHelp() {
    std::printf("\n道具效果一览：\n");
    for (int i = 0; i < ITEM_COUNT; ++i)
        std::printf("%s", kItems[i].help);
    std::printf("（看完仍然是你的回合，可以继续行动）\n");
}
```

效果文字复用规则说明里已有的 `kItems[].help`，不另写一份，避免两处不同步。

### 接进两个模式

单人模式（`playerStep()`）与双人模式（`playDuo()`）都在读取选择之后、
判断"用道具/开枪"之前插入同一个分支：

```cpp
const int choice = askNumber(1, kMenuLast);

if (choice == kMenuItemHelp) {          // 查看道具效果
    showItemHelp();
    waitMs(1200);
    clearScreen();
    return StepResult::Continue;        // 单人：继续本回合
    // 双人：continue;（不换人）
}
```

关键是返回 `Continue` / `continue`：

- **不消耗行动** —— 和"使用道具"一样，回到菜单让玩家重新选；
- **不换人** —— 双人模式里 `usedAction` 保持 `false`，`cur` 不切换；
- **不消耗道具** —— 完全没碰 `items[]`；
- **不判定胜负** —— 没走任何开枪路径。

### 顺手更新了规则说明

规则页末尾加了一句提示，让玩家知道战斗中随时能重看：

```
战斗中随时可以选 8 号选项重看这份道具效果，查看不会消耗你的行动。
```

## 玩家看到的效果

```
你要
1.向恶魔开枪
2.向自己开枪
3.使用放大镜
4.使用手铐
5.使用小刀
6.使用烟
7.使用饮料
8.查看道具效果          <-- 输入 8
                       
道具效果一览：
放大镜：可以知道下一发子弹是空弹还是实弹
手铐：增加一次本回合的行动次数
小刀：若下一发子弹为实弹，则伤害+1
烟：可以回复1点体力
饮料：可以退一发子弹
（看完仍然是你的回合，可以继续行动）

你要                    <-- 回到菜单，仍然是同一回合
1.向恶魔开枪
...
```

## 验证结果

在本机重编译后实测（脚本 `__recovery/test_opt8.py`）：

| 检查项 | 结果 |
|---|---|
| 菜单出现 `8.查看道具效果` | ✅ |
| 选择 8 后打印五种道具效果 | ✅ |
| 效果看完后紧接的下一个界面仍是行动菜单 | ✅ |
| 效果页与下一次菜单之间**没有出现恶魔回合** | ✅ 3/3 次无违例 |
| 道具数量不因查看而变化 | ✅ |
| 血量 / 剩余子弹数不因查看而变化 | ✅ |
| 双人模式同样提供该选项且不换人 | ✅ |

---

## 顺带修掉的一个真 bug：清屏会吃掉输出

做这个功能时测试脚本突然只收到 `0x0C`（`cls` 的控制字符）而看不到任何文字，
排查后发现是一个**独立存在的输出 bug**，之前一直被掩盖：

```cpp
// 错误写法
static void clearScreen() { std::system("cls"); }
```

`system()` 只会刷新 **C 运行库自己缓冲区**里的内容，而我们的 `printf`
文本还留在 stdio 缓冲里没出去；紧接着的 `cls` 把屏幕（以及重定向时的输出）
清掉，这部分内容就永远丢了。

- 在**真实控制台交互**下通常看不出来（终端一般是行缓冲，换行就刷了）；
- 但**一旦重定向输出**（管道、写文件、自动化测试）就变成满屏只有 `0x0C`。

修法是在清屏前先把自己的缓冲刷出去：

```cpp
static void clearScreen() {
    std::fflush(stdout);      // 先把自己的输出落盘/落屏
    std::system("cls");
}
static void pauseScreen() {
    std::fflush(stdout);
    std::system("pause");
}
```

实测：修复前一次会话只能收到 4634 字节的纯 `0x0C`；
修复后同一会话能拿到完整的 2000+ 字节正常文本。忠实还原版也同步修了。
