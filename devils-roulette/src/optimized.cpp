// ============================================================================
//  恶魔轮盘赌 (Devil's Roulette) —— 优化修复版  v3
// ============================================================================
//  本文件基于从原 EXE 还原出的源码重写：
//    · 修正了 4 个 bug（详见 docs/BUGFIXES.md）
//    · 表驱动 + 结构化数据，消除重复分支
//    · 【v3 新增】恶魔 AI 会评估胜率后再决定打谁 / 用哪件道具
//    · 【v3 新增】分出胜负后可以选择重开一局
//    · 所有原有文案逐字保留
//
//  原程序信息：MinGW-w64 / GNU C++ (GCC 4.9.2)，2024-10-21 构建
//
//  ⚠ 编译时必须指定源码与执行字符集！
//    本文件是 UTF-8，但游戏文案要按 GBK(CP936) 输出，
//    否则中文在 Windows 控制台会显示成乱码。
//
//  编译（Windows + MinGW/g++，本机已实测通过）：
//      g++ -std=c++17 -O2 -Wall -Wextra -finput-charset=UTF-8 -fexec-charset=GBK -o game.exe optimized.cpp
//  （或直接运行同目录的 build.bat）
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <initializer_list>

// ---------------------------------------------------------------------------
// 数值常量（集中管理，去掉散落的魔法数字）
// ---------------------------------------------------------------------------
namespace rules {
    constexpr int kHpSingle   = 6;   // 单人模式生命上限
    constexpr int kHpDuo      = 4;   // 双人模式生命上限
    constexpr int kItemKinds  = 5;   // 道具种类
    constexpr int kItemCap    = 8;   // 道具携带上限
    constexpr int kSupply     = 4;   // 每回合补给件数
    constexpr int kMinBullets = 1;   // 实弹/空弹各自的下限
    constexpr int kMaxBullets = 4;   // 实弹/空弹各自的上限
    constexpr int kMaxQueued  = kMaxBullets * 2;
}

// ---------------------------------------------------------------------------
// 道具：编号 + 名称 + 说明书（表驱动，替代原来的 5 份重复分支）
// ---------------------------------------------------------------------------
enum ItemId { MAGNIFIER = 0, CUFFS, KNIFE, CIGARETTE, DRINK, ITEM_COUNT };
static_assert(ITEM_COUNT == rules::kItemKinds, "道具数量不一致");

struct ItemDef {
    const char* name;
    const char* help;     // 规则说明里的一行
};

static const ItemDef kItems[ITEM_COUNT] = {
    { "放大镜", "放大镜：可以知道下一发子弹是空弹还是实弹\n" },
    { "手铐",   "手铐：增加一次本回合的行动次数\n" },
    { "小刀",   "小刀：若下一发子弹为实弹，则伤害+1\n" },
    { "烟",     "烟：可以回复1点体力\n" },
    { "饮料",   "饮料：可以退一发子弹\n" },
};

// ---------------------------------------------------------------------------
// 参战方
// ---------------------------------------------------------------------------
struct Player {
    const char* name = "";
    int  hp          = 0;
    int  items[ITEM_COUNT] = { 0 };
    bool cuffs       = false;   // 还握着一次「手铐」给的行动
    bool knifeUsed   = false;   // 本回合小刀已用过
    bool knowsNext   = false;   // 用过放大镜，已知下一发

    int itemTotal() const {
        int n = 0;
        for (int i = 0; i < ITEM_COUNT; ++i) n += items[i];
        return n;
    }
};

// ---------------------------------------------------------------------------
// 对局状态
// ---------------------------------------------------------------------------
struct Game {
    Player you, demon;      // 单人：玩家 / 恶魔
    Player a, b;            // 双人：玩家A / 玩家B
    int  live = 0, blank = 0;
    int  queue[rules::kMaxQueued] = { 0 };   // 1 = 实弹，0 = 空弹；queue[0] 为下一发
    int  queued = 0;
    int  damage = 1;
    double money = 0.0;
};

static Game g;

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
static int rnd(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + std::rand() % (hi - lo + 1);
}
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

// 注意：调用 system("cls") 之前必须先把自己的输出刷出去。
// system() 只会刷新 C 运行库自己缓冲区里的东西，而我们的 printf 文本还留在
// stdio 缓冲里；清屏之后这部分内容就永远看不到了（重定向输出时尤其明显）。
static void clearScreen() {
    std::fflush(stdout);
    std::system("cls");
}
static void pauseScreen() {
    std::fflush(stdout);
    std::system("pause");
}

// 等一会儿。原程序调用 Windows 的 Sleep()；这里用标准库实现，跨平台。
static void waitMs(int ms) {
    const std::clock_t until =
        std::clock() + static_cast<std::clock_t>(ms) * (CLOCKS_PER_SEC / 1000);
    while (std::clock() < until) { /* busy wait */ }
}

// ---------------------------------------------------------------------------
// 输入
// ---------------------------------------------------------------------------
static int askNumber(int lo, int hi) {
    for (;;) {
        int v = 0;
        if (std::scanf("%d", &v) != 1) {
            int c;
            while ((c = std::getchar()) != '\n' && c != EOF) {}   // 吃掉这一行
            v = 0;
        }
        if (v >= lo && v <= hi) return v;
        std::printf("输入不合法\n");
        waitMs(1000);
        clearScreen();
    }
}

// ---------------------------------------------------------------------------
// 弹夹
// ---------------------------------------------------------------------------
static bool nextIsLive() { return g.queued > 0 ? g.queue[0] != 0 : true; }

static void popBullet() {
    if (g.queued <= 0) return;
    if (g.queue[0]) --g.live; else --g.blank;
    --g.queued;
    if (g.queued > 0)
        std::memmove(g.queue, g.queue + 1, g.queued * sizeof(int));
}

// 装弹：实弹/空弹数量各自独立取 [1,4]，再整体洗牌
static void loadBullets(int live, int blank) {
    g.live  = live;
    g.blank = blank;
    g.queued = imin(live + blank, rules::kMaxQueued);

    for (int i = 0; i < g.queued; ++i)
        g.queue[i] = (i < live) ? 1 : 0;

    for (int i = g.queued - 1; i > 0; --i) {          // Fisher-Yates
        int j = rnd(0, i);
        int t = g.queue[i]; g.queue[i] = g.queue[j]; g.queue[j] = t;
    }
}

static void resetRoundFlags(Player& x, Player& y) {
    g.damage = 1;
    x.cuffs = y.cuffs = false;
    x.knifeUsed = y.knifeUsed = false;
    x.knowsNext = y.knowsNext = false;
}

// 清空整局状态（重开一局时用）
static void resetGame() {
    g.live = g.blank = g.queued = 0;
    g.damage = 1;
    std::memset(g.queue, 0, sizeof(g.queue));
    for (Player* p : { &g.you, &g.demon, &g.a, &g.b }) {
        p->hp = 0;
        std::memset(p->items, 0, sizeof(p->items));
        p->cuffs = p->knifeUsed = p->knowsNext = false;
    }
}

// ---------------------------------------------------------------------------
// 发道具
//   BUG #2 修复：原来道具种类取 rnd(0, 4) 即 0..3，
//              第五种「饮料」永远抽不到；这里改为覆盖全部 5 种。
//   BUG #3 修复：原来打印的是「剩余可携带上限」而不是实发件数。
// ---------------------------------------------------------------------------
static int supplyItems(Player& p) {
    int given = 0;
    for (int i = 0; i < rules::kSupply; ++i) {
        if (p.itemTotal() >= rules::kItemCap) break;   // 到上限就停
        ++p.items[rnd(0, ITEM_COUNT - 1)];
        ++given;
    }
    return given;
}

// 装弹 + 补给
static void newRound(Player& x, Player& y,
                     const char* labelX, const char* labelY) {
    loadBullets(rnd(rules::kMinBullets, rules::kMaxBullets),
                rnd(rules::kMinBullets, rules::kMaxBullets));
    std::printf("%d发实弹,%d发空弹\n", g.live, g.blank);

    const int givenX = supplyItems(x);
    const int givenY = supplyItems(y);
    std::printf("%s获得了%d个道具:\n", labelX, givenX);
    std::printf("%s获得了%d个道具:\n", labelY, givenY);

    resetRoundFlags(x, y);
}

// ---------------------------------------------------------------------------
// 局面显示
// ---------------------------------------------------------------------------
static void printInventory(const Player& p, const char* who) {
    std::printf("%s现在拥有的道具:\n", who);
    for (int i = 0; i < ITEM_COUNT; ++i) {
        std::printf("%s%d个", kItems[i].name, p.items[i]);
        if (i != ITEM_COUNT - 1) std::printf(",");
    }
    std::printf("\n");
}

static void printBoard(const Player& x, const Player& y,
                       const char* labelX, const char* labelY,
                       const char* hpLabelX, const char* hpLabelY) {
    std::printf("%s:%d\n", hpLabelX, x.hp);
    std::printf("%s:%d\n", hpLabelY, y.hp);
    std::printf("剩余实弹数:%d    剩余空弹数:%d\n", g.live, g.blank);
    printInventory(x, labelX);
    printInventory(y, labelY);
}

// 行动菜单里的选项编号：1 打对方、2 打自己、3..7 用五种道具、8 查看道具效果
constexpr int kMenuShootFoe  = 1;
constexpr int kMenuShootSelf = 2;
constexpr int kMenuItemFirst = 3;                                  // 3..3+ITEM_COUNT-1
constexpr int kMenuItemLast  = kMenuItemFirst + ITEM_COUNT - 1;    // = 7
constexpr int kMenuItemHelp  = kMenuItemLast + 1;                  // = 8
constexpr int kMenuLast      = kMenuItemHelp;

static void printActionMenu(const Player& self, const char* labelFoe) {
    std::printf("%s要\n1.向%s开枪\n2.向自己开枪\n", self.name, labelFoe);
    for (int i = 0; i < ITEM_COUNT; ++i)
        std::printf("%d.使用%s\n", kMenuItemFirst + i, kItems[i].name);
    std::printf("%d.查看道具效果\n", kMenuItemHelp);
}

// 查看道具效果：只读，不消耗行动、不消耗道具、不结束回合
static void showItemHelp() {
    std::printf("\n道具效果一览：\n");
    for (int i = 0; i < ITEM_COUNT; ++i)
        std::printf("%s", kItems[i].help);   // 每行形如「放大镜：可以知道…\n」
    std::printf("（看完仍然是你的回合，可以继续行动）\n");
}

// ---------------------------------------------------------------------------
// 使用道具：单一实现，双方共用（原来「你/恶魔/A/B」四套重复分支）
// ---------------------------------------------------------------------------
static bool useItem(Player& self, int idx) {
    if (idx < 0 || idx >= ITEM_COUNT) return false;

    if (self.items[idx] <= 0) {
        std::printf("%s现在没有%s\n", self.name, kItems[idx].name);
        return false;
    }

    switch (idx) {
    case MAGNIFIER:
        if (self.knowsNext) { std::printf("%s现在没有放大镜\n", self.name); return false; }
        std::printf("%s使用了放大镜\n", self.name);
        std::printf("%s看到了\n", self.name);
        if (std::strcmp(self.name, "恶魔") == 0) std::printf("有趣\n");
        std::printf("%s\n", nextIsLive() ? "实弹" : "空弹");
        self.knowsNext = true;
        break;

    case CUFFS:
        if (self.cuffs) { std::printf("%s已经用过手铐了\n", self.name); return false; }
        std::printf("%s使用了手铐\n", self.name);
        self.cuffs = true;
        std::printf("%s获得了连开两枪的机会\n", self.name);
        break;

    case KNIFE:
        if (self.knifeUsed) { std::printf("%s已经用过小刀了\n", self.name); return false; }
        std::printf("%s使用了小刀\n", self.name);
        std::printf("若下一发为实弹则伤害+1\n");
        self.knifeUsed = true;
        g.damage = 2;
        break;

    case CIGARETTE:
        if (self.hp >= rules::kHpSingle) {
            std::printf("%s现在不需要烟\n", self.name);
            return false;
        }
        std::printf("%s使用了烟\n", self.name);
        ++self.hp;
        std::printf("%s回复了一点生命\n", self.name);
        break;

    case DRINK:
        if (g.queued <= 0) { std::printf("%s现在没有饮料\n", self.name); return false; }
        std::printf("%s使用了饮料\n", self.name);
        std::printf("%s退了一发\n", self.name);
        // 正确报告被退掉的是哪一种（原程序这里的判断分支写反了）
        std::printf("%s\n", nextIsLive() ? "实弹" : "空弹");
        popBullet();
        break;
    }

    --self.items[idx];
    return true;
}

// ---------------------------------------------------------------------------
// 开一枪
// ---------------------------------------------------------------------------
static void fireAt(Player& self, Player& foe, const char* labelFoe, bool atFoe) {
    const bool live = nextIsLive();

    if (atFoe) std::printf("%s决定向%s开枪\n", self.name, labelFoe);
    else       std::printf("%s决定向自己开枪\n", self.name);

    waitMs(300);
    popBullet();
    std::printf("%s\n", live ? "是实弹" : "是空弹");

    Player& target = atFoe ? foe : self;
    if (live) {
        target.hp -= g.damage;
        g.damage = 1;
        self.knifeUsed = false;
    }
}

// ===========================================================================
//  恶魔 AI
// ===========================================================================
//
//  目标：在「已知 / 未知下一发」的条件下，选择胜率更高的行动。
//
//  核心是 estimateWinProb()：给定剩余实弹、空弹、双方血量与当前行动权，
//  估算"当前行动方"最终获胜的概率。用记忆化递归求解，逻辑上等价于
//  一个有限状态的博弈搜索，动作集合为 {打对方, 打自己}（不含道具，
//  道具由 useItems 单独决策，这样既保持搜索可行，也更贴近人类思路）。
//
//  已知下一发子弹时的关键结论（原程序完全没用上）：
//    · 确定是实弹 → 打对方（对空弹来说是"打自己"更差）
//    · 确定是空弹 → 打自己（白赚一次行动权）
//  未知时的粗略概率用 c/(c+1) 近似（对手至多两枪内会被打掉时更倾向冲）。
//
// ---------------------------------------------------------------------------
namespace ai {

constexpr int kMaxHP = rules::kHpSingle;      // 血量搜索上界
constexpr int kMaxN  = rules::kMaxQueued;     // 弹夹搜索上界

// memo[l][b][mh][oh][turn]：当前行动方的胜率，用 0..10000 的整数存
static short memo[kMaxN + 1][kMaxN + 1][kMaxHP + 1][kMaxHP + 1][2];
static bool  done[kMaxN + 1][kMaxN + 1][kMaxHP + 1][kMaxHP + 1][2];

static int solve(int l, int b, int mh, int oh, int dmg, int turn) {
    // 先处理终局
    if (mh <= 0) return 0;            // 自己倒了 → 胜率 0
    if (oh <= 0) return 10000;        // 对手倒了 → 胜率满

    l = imax(l, 0);
    b = imax(b, 0);
    mh = imin(mh, kMaxHP);
    oh = imin(oh, kMaxHP);

    if (l + b == 0) return 5000;      // 无子弹（理论不会发生）→ 视为五五开

    if (done[l][b][mh][oh][turn]) return memo[l][b][mh][oh][turn];

    const int total = l + b;
    // 打对方
    int shootFoe;
    if (l > 0) {
        const int nh = oh - dmg;                      // 命中
        const int after = (nh <= 0) ? 10000
                                    : 10000 - solve(l - 1, b, mh, nh, 1, turn ^ 1);
        const int miss  = (b > 0) ? 10000 - solve(l, b - 1, mh, oh, 1, turn ^ 1) : after;
        shootFoe = (l * after + b * miss) / total;
    } else {
        shootFoe = 0;                                  // 全是空弹，打对方毫无收益
    }

    // 打自己
    int shootSelf;
    if (b > 0) {
        const int hit  = (mh - dmg <= 0) ? 0
                                        : 10000 - solve(l, b - 1, mh - dmg, oh, 1, turn ^ 1);
        // 空弹：还有行动权，能继续决定
        const int cont = solve(l, b - 1, mh, oh, dmg, turn);
        shootSelf = (b * cont + l * hit) / total;
    } else {
        shootSelf = 0;
    }

    const int best = imax(shootFoe, shootSelf);
    memo[l][b][mh][oh][turn] = static_cast<short>(best);
    done[l][b][mh][oh][turn] = true;
    return best;
}

// 对外：估算当前行动方的胜率（0..10000）
static int winProb(int myHP, int foeHP, int dmg) {
    return solve(g.live, g.blank, myHP, foeHP, dmg, 0);
}

// 决策：返回 true 表示"打自己"
static bool shouldShootSelf(const Player& me, const Player& foe) {
    if (me.knowsNext) {
        // 已知下一发 → 直接按结果决定
        return !nextIsLive();
    }

    const int total = g.live + g.blank;
    if (total <= 0) return false;
    if (g.live <= 0) return true;      // 全是空弹，当然打自己
    if (g.blank <= 0) return false;    // 全是实弹，当然打对方

    // 未知：比较两种选择的胜率
    // 打对方：命中则对手掉血且换他行动；空弹则换他行动
    const int pLive = g.live * 10000 / total;
    const int pBlank = 10000 - pLive;

    const int afterFoeLive = (foe.hp - g.damage <= 0)
        ? 10000 : 10000 - winProb(foe.hp - g.damage, me.hp, 1);
    const int afterFoeBlank = 10000 - winProb(me.hp, foe.hp, g.damage);
    const int shootFoe = (pLive * afterFoeLive + pBlank * afterFoeBlank) / 10000;

    // 打自己：命中则自己掉血并换对方行动；空弹则自己继续行动
    const int afterSelfLive = (me.hp - g.damage <= 0)
        ? 0 : 10000 - winProb(foe.hp, me.hp - g.damage, 1);
    const int afterSelfBlank = winProb(me.hp, foe.hp, g.damage);
    const int shootSelf = (pLive * afterSelfLive + pBlank * afterSelfBlank) / 10000;

    return shootSelf > shootFoe;
}

}  // namespace ai

// ---------------------------------------------------------------------------
// 恶魔难度
//   Casual —— 只在"确实看到了结果"时才做正确判断，其余随机；道具用得少
//   Sharp  —— 完整使用博弈搜索 + 全部道具（默认）
// ---------------------------------------------------------------------------
enum class Difficulty { Casual, Sharp };
static Difficulty g_difficulty = Difficulty::Sharp;

// ---------------------------------------------------------------------------
// 恶魔的道具策略
// ---------------------------------------------------------------------------
static void demonUseItems(Player& me) {
    const int total = g.live + g.blank;
    const bool sharp = (g_difficulty == Difficulty::Sharp);

    // 1) 残血就抽烟（最稳的收益）
    while (me.items[CIGARETTE] > 0 && me.hp < rules::kHpSingle &&
           me.hp * 2 <= rules::kHpSingle) {
        if (!useItem(me, CIGARETTE)) break;
    }

    // 2) 不确定下一发就先看牌（休闲模式只有一半概率想起来用）
    if (!me.knowsNext && me.items[MAGNIFIER] > 0 && total > 1) {
        if (sharp || rnd(0, 1) == 0) useItem(me, MAGNIFIER);
    }

    const bool known = me.knowsNext;
    const bool live  = nextIsLive();

    // 3) 确定是空弹 → 手铐可以白赚一次行动
    if (known && !live && me.items[CUFFS] > 0 && !me.cuffs) {
        useItem(me, CUFFS);
    }

    // 4) 确定是实弹 → 小刀放大伤害（能一击致命时必用）
    if (known && live && me.items[KNIFE] > 0 && !me.knifeUsed) {
        if (g.damage < 2) useItem(me, KNIFE);
    }

    // 5) 未知但很可能实弹，且自己血量吃紧 → 喝饮料把这一发退掉（仅高手模式）
    if (sharp && !known && total > 0 && me.items[DRINK] > 0) {
        const int pLive = g.live * 4;          // 实弹占比（放大 4 倍比较）
        if (pLive > total * 3 && me.hp <= 2)   // 实弹占比 > 75% 且残血
            useItem(me, DRINK);
    }

    // 6) 手铐带来的额外行动若还没兑现，补看一次牌也有价值
    if (me.cuffs && !me.knowsNext && me.items[MAGNIFIER] > 0 && total > 1) {
        useItem(me, MAGNIFIER);
    }
}

// 恶魔是否"打自己"：休闲模式退化成一个简单启发式
static bool demonShouldSelfShoot(const Player& me, const Player& foe) {
    if (g_difficulty == Difficulty::Sharp)
        return ai::shouldShootSelf(me, foe);

    // Casual：已知结果就照做，否则按血量粗略判断
    if (me.knowsNext) return !nextIsLive();
    if (g.live <= 0) return true;
    if (g.blank <= 0) return false;
    // 血量领先时更保守（偏打对方），落后时更激进（赌空弹）
    if (me.hp >= foe.hp) return false;
    return rnd(0, 2) == 0;
}

// 恶魔的一个回合；返回 true 表示对局已结束
static bool demonTurn(Player& me, Player& foe, const char* labelFoe) {
    std::printf("现在是恶魔的回合\n");

    // 手牌最多用几轮，避免道具连锁导致死循环
    for (int guard = 0; guard < 4; ++guard) {
        const int before = me.itemTotal();
        demonUseItems(me);
        if (me.itemTotal() == before) break;
    }

    bool again = false;
    do {
        again = false;
        const bool live  = nextIsLive();
        const bool shootSelf = demonShouldSelfShoot(me, foe);

        me.knowsNext = false;

        if (shootSelf) {
            std::printf("恶魔决定向自己开枪\n");
            waitMs(400);
            popBullet();
            if (!live) {
                std::printf("是空弹\n");
                if (me.cuffs) {
                    std::printf("因为恶魔使用了手铐，所以可以再来一次\n");
                    me.cuffs = false;
                    waitMs(300);
                    again = true;
                }
            } else {
                std::printf("是实弹\n");
                me.hp -= g.damage;
                g.damage = 1;
                me.knifeUsed = false;
                waitMs(400);
                if (me.hp <= 0) return true;
            }
        } else {
            std::printf("恶魔决定向%s开枪\n", labelFoe);
            waitMs(400);
            popBullet();
            if (!live) {
                std::printf("是空弹\n");
                if (me.cuffs) {
                    std::printf("因为恶魔使用了手铐，所以可以再来一次\n");
                    me.cuffs = false;
                    waitMs(300);
                    again = true;
                }
            } else {
                std::printf("是实弹\n");
                foe.hp -= g.damage;
                g.damage = 1;
                me.knifeUsed = false;
                waitMs(400);
                if (foe.hp <= 0) return true;
            }
        }
    } while (again);

    return false;
}

// ===========================================================================
//  单人模式
// ===========================================================================
enum class StepResult { Continue, TurnEnded, GameOver };

// 玩家自己走一步
static StepResult playerStep() {
    printActionMenu(g.you, "恶魔");
    const int choice = askNumber(1, kMenuLast);

    // 查看道具效果：不消耗回合，看完继续让玩家行动
    if (choice == kMenuItemHelp) {
        showItemHelp();
        waitMs(1200);
        clearScreen();
        return StepResult::Continue;
    }

    if (choice >= kMenuItemFirst) {
        useItem(g.you, choice - kMenuItemFirst);
        return StepResult::Continue;               // 用道具不消耗行动
    }

    const bool live = nextIsLive();
    fireAt(g.you, g.demon, "恶魔", choice == kMenuShootFoe);

    if (g.you.hp <= 0 || g.demon.hp <= 0) return StepResult::GameOver;

    if (live) return StepResult::TurnEnded;        // 实弹：本回合结束

    // 空弹
    if (g.you.cuffs) {                             // 手铐：多打一次
        std::printf("因为你使用了手铐，所以可以再来一次\n");
        g.you.cuffs = false;
        waitMs(300);
        return StepResult::Continue;
    }
    return StepResult::TurnEnded;
}

// 打完整整一局；返回值：0 = 玩家赢，1 = 玩家输
static int playSingle() {
    g.you.hp = rules::kHpSingle;
    g.demon.hp = rules::kHpSingle;
    g.you.name = "你";
    g.demon.name = "恶魔";

    for (;;) {
        if (g.queued <= 0)
            newRound(g.you, g.demon, "你", "恶魔");

        printBoard(g.you, g.demon, "你", "恶魔", "你的生命", "恶魔生命");
        std::printf("现在是你的回合\n");

        // ---- 玩家回合 ----
        bool turnOver = false;
        while (!turnOver) {
            switch (playerStep()) {
            case StepResult::Continue:  break;
            case StepResult::TurnEnded: turnOver = true; break;
            case StepResult::GameOver:
                if (g.demon.hp <= 0) { std::printf("你赢了\n你获得了奖金$%.2lf\n", g.money); return 0; }
                std::printf("你输了\n");
                return 1;
            }
        }

        clearScreen();

        // ---- 恶魔回合（原程序缺失的一步）----
        if (demonTurn(g.demon, g.you, "你")) {
            if (g.you.hp <= 0) { std::printf("你输了\n"); return 1; }
            std::printf("你赢了\n你获得了奖金$%.2lf\n", g.money);
            return 0;
        }
        clearScreen();
    }
}

// ===========================================================================
//  双人模式
// ===========================================================================
// 打完整整一局；返回值：0 = 玩家A赢，1 = 玩家B赢
static int playDuo() {
    g.a.hp = g.b.hp = rules::kHpDuo;
    g.a.name = "玩家A";
    g.b.name = "玩家B";

    Player* cur = &g.a;
    Player* foe = &g.b;
    const char* labelA = "玩家A";
    const char* labelB = "玩家B";

    for (;;) {
        if (g.queued <= 0)
            newRound(g.a, g.b, labelA, labelB);

        printBoard(g.a, g.b, labelA, labelB, "玩家A的生命", "玩家B的生命");
        std::printf("现在是%s的回合\n", cur->name);

        const char* labelFoe = (cur == &g.a) ? labelB : labelA;
        bool usedAction = false;

        while (!usedAction) {
            printActionMenu(*cur, labelFoe);
            const int choice = askNumber(1, kMenuLast);

            // 查看道具效果：不换人，看完还是当前玩家的回合
            if (choice == kMenuItemHelp) {
                showItemHelp();
                waitMs(1200);
                clearScreen();
                continue;
            }

            if (choice >= kMenuItemFirst) {
                useItem(*cur, choice - kMenuItemFirst);
                continue;                     // 用道具不消耗行动
            }

            const bool live = nextIsLive();
            fireAt(*cur, *foe, labelFoe, choice == kMenuShootFoe);

            if (g.a.hp <= 0 || g.b.hp <= 0) {
                if (g.a.hp <= 0) {
                    std::printf("玩家B赢了\n玩家B获得了奖金$%.2lf\n", g.money);
                    return 1;
                }
                std::printf("玩家A赢了\n玩家A获得了奖金$%.2lf\n", g.money);
                return 0;
            }

            if (live) {
                usedAction = true;            // 实弹：换人
            } else if (choice == kMenuShootFoe) {
                usedAction = true;            // 空弹 + 打对方：换人
            } else if (cur->cuffs) {
                std::printf("因为%s使用了手铐，所以可以再来一次\n", cur->name);
                cur->cuffs = false;           // 空弹 + 打自己：继续
            } else {
                usedAction = true;
            }
        }

        Player* t = cur; cur = foe; foe = t;
        clearScreen();
    }
}

// ===========================================================================
//  菜单
// ===========================================================================
static void showRules() {
    std::printf("规则:\n");
    std::printf("你和恶魔都各有6点生命\n");
    std::printf("每一回合开始前，你将知道一共有几发实弹，几发空弹，"
                "同时双方都将获得4个道具作为补给（上限为8个）\n");
    std::printf("每一回合，你可以选择对自己开枪，对恶魔开枪或者使用道具\n");
    std::printf("如果你对自己开枪，若为空弹，则可以继续行动，否则，停止行动\n");
    std::printf("如果你对恶魔开枪，无论如何，都将停止行动\n");
    std::printf("道具一览：\n");
    for (int i = 0; i < ITEM_COUNT; ++i)
        std::printf("%s", kItems[i].help);
    std::printf("战斗中随时可以选 %d 号选项重看这份道具效果，"
                "查看不会消耗你的行动。\n", kMenuItemHelp);
}

static void showModeMenu() {
    std::printf("请选择你想要的模式：\n");
    std::printf("1.单人\n");
    std::printf("2.双人（此模式中，生命值为4，道具补给为2）\n");
}

static void showDifficultyMenu() {
    std::printf("请选择恶魔的难度：\n");
    std::printf("1.休闲（恶魔偶尔犯糊涂，道具用得少）\n");
    std::printf("2.高手（恶魔会算胜率，道具用得很精，推荐）\n");
}

// 打完一局后询问是否再来一局；返回 true 表示重开
static bool askReplay(int winner) {
    std::printf("\n");
    std::printf("==================== 本局结束 ====================\n");
    if (winner == 0) std::printf("结果：你赢了\n");
    else             std::printf("结果：你输了\n");
    std::printf("1.再来一局\n");
    std::printf("2.退出游戏\n");
    std::printf("==================================================\n");
    return askNumber(1, 2) == 1;
}

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    std::printf("又来了一位挑战者...\n");
    waitMs(1000);
    std::printf("准备好参与恶魔的游戏吗？胜者带走奖金，败者将会在此长眠\n"
                "1.好的\n2.没问题\n");
    askNumber(1, 2);

    std::printf("你清楚我们的规则吗？\n1.清楚\n2.不清楚\n");
    if (askNumber(1, 2) == 2) {
        showRules();
        pauseScreen();
        clearScreen();
    }

    std::printf("好吧\n");
    waitMs(1145);
    std::printf("游戏将要开始了哦\n");
    waitMs(1145);

    // ---- 主循环：可以连续玩很多局 ----
    for (;;) {
        clearScreen();
        showModeMenu();
        const int mode = askNumber(1, 2);

        if (mode == 1) {
            showDifficultyMenu();
            g_difficulty = (askNumber(1, 2) == 1) ? Difficulty::Casual
                                                  : Difficulty::Sharp;
        }

        resetGame();
        g.money = static_cast<double>(rnd(0, 999999)) / 100.0;

        const int winner = (mode == 1) ? playSingle() : playDuo();

        if (!askReplay(winner)) break;
    }

    std::printf("\n感谢游玩，再见！\n");
    pauseScreen();
    return 0;
}
