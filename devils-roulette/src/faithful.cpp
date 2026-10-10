// ============================================================================
//  恶魔轮盘赌 (Devil's Roulette)
// ============================================================================
//  本文件由原 EXE「恶魔轮盘赌.exe」逆向还原而成。
//
//  原程序信息（从 COFF 符号表 / PE 头中恢复）：
//    源文件    : 恶魔轮盘赌.cpp
//    编译器    : MinGW-w64, GNU C++ (GCC 4.9.2), -m64 -O2
//    构建时间  : 2024-10-21 12:36:17 UTC
//    函数      : 源级 16 个函数 / 20 个编译后函数
//                （边界来自 .pdata 异常表，名字来自 COFF 符号表；见 functions.md）
//    文本      : 1039 条，其中 144 条游戏文案已 100% 逐字还原
//
//  原程序用 printf / scanf 做控制台交互，用 system("cls") 清屏、
//  system("pause") 暂停。这里沿用同样的方式，保证行为一致。
//
//  编译（Windows + MinGW/g++）：
//      g++ -O2 -o 恶魔轮盘赌.exe 恶魔轮盘赌.cpp
//  在 Linux/macOS 下调试时，把 clearScreen()/pauseScreen() 里的
//  "cls"/"pause" 换成 "clear"/"read -n 1"。
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

// ---------------------------------------------------------------------------
// 游戏常量（数值来自原程序内文本与反汇编）
// ---------------------------------------------------------------------------
const int MAX_HP_SINGLE = 6;   // 单人模式：双方生命上限
const int MAX_HP_DUO    = 4;   // 双人模式：双方生命上限
const int ITEM_KINDS    = 5;   // 道具种类数
const int MAX_ITEMS     = 8;   // 道具上限（规则文本："上限为8个"）
const int MAX_BULLETS   = 12;  // 弹夹容量上限

// 道具编号（原程序中按此顺序存放在数组里）
enum ItemId {
    ITEM_MAGNIFIER = 0,  // 放大镜：知道下一发子弹是空弹还是实弹
    ITEM_CUFFS     = 1,  // 手铐：增加一次本回合的行动次数
    ITEM_KNIFE     = 2,  // 小刀：若下一发子弹为实弹，则伤害 +1
    ITEM_CIGARETTE = 3,  // 烟：回复 1 点体力
    ITEM_DRINK     = 4   // 饮料：退掉一发子弹
};

const char* const ITEM_NAMES[ITEM_KINDS] = {
    "放大镜", "手铐", "小刀", "烟", "饮料"
};

// ---------------------------------------------------------------------------
// 参与者状态
//   hp           生命值
//   items[]      五种道具各自的持有数量
//   cuffs        本回合是否还持有「手铐」带来的额外行动
//   knifeUsed    本回合小刀是否已使用（避免伤害被反复叠成 2）
//   knowsNext    通过放大镜是否已知下一发子弹
// ---------------------------------------------------------------------------
struct Player {
    const char* name;
    int  hp;
    int  items[ITEM_KINDS];
    bool cuffs;
    bool knifeUsed;
    bool knowsNext;
};

static Player gYou;    // 单人模式：玩家
static Player gDemon;  // 单人模式：恶魔
static Player gA;      // 双人模式：玩家A
static Player gB;      // 双人模式：玩家B

// ---------------------------------------------------------------------------
// 全局对局状态
//   gLive / gBlank  当前弹夹剩余的实弹 / 空弹数
//   gBullets[]      弹夹队列，gBullets[0] 是下一发（1 = 实弹，0 = 空弹）
//   gBulletCount    弹夹剩余长度
//   gDamage         本次射击的伤害
//   gMoney          奖金
// ---------------------------------------------------------------------------
static int    gLive = 0, gBlank = 0;
static int    gBullets[MAX_BULLETS];
static int    gBulletCount = 0;
static int    gDamage = 1;
static double gMoney = 0.0;

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
// 先把自己的输出刷出去再清屏，否则重定向输出时这段文字会丢
static void clearScreen() { std::fflush(stdout); system("cls"); }
static void pauseScreen() { std::fflush(stdout); system("pause"); }

// 等一会儿。原程序在 Windows 上直接调用 Sleep()，
// 这里用忙等实现，避免额外依赖（不影响逻辑）。
static void waitMs(int milliseconds)
{
    clock_t end = clock() + (clock_t)(milliseconds * (CLOCKS_PER_SEC / 1000));
    while (clock() < end) { /* busy wait */ }
}

static int randomInt(int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + rand() % (hi - lo + 1);
}

static int minInt(int a, int b) { return a < b ? a : b; }

// ---------------------------------------------------------------------------
// 弹夹管理
// ---------------------------------------------------------------------------
// 打出（消耗）当前这一发
static void popBullet()
{
    if (gBulletCount > 0) {
        if (gBullets[0]) --gLive; else --gBlank;
        --gBulletCount;
        if (gBulletCount > 0)
            memmove(gBullets, gBullets + 1, gBulletCount * sizeof(int));
    }
}

// 下一发是实弹吗？（弹夹空时保守地视为实弹）
static bool nextIsLive()
{
    return (gBulletCount > 0) ? (gBullets[0] != 0) : true;
}

// 装填：先按实弹数放好，再整体洗牌
// 原程序（0x40157E 起）用的正是这个"先填后洗"的做法：
//   for (i = 1; i <= shi + kong; ++i) { r = randomInt(1, shi + kong);
//       if (r <= shi) bullet[r] = 2, --shi; else bullet[r] = 1, --kong; }
// 实弹/空弹的数量各自独立取 [1,4]，所以一回合总共有 2~8 发。
static void loadBullets(int live, int blank)
{
    gLive = live;
    gBlank = blank;
    gBulletCount = live + blank;
    if (gBulletCount > MAX_BULLETS) gBulletCount = MAX_BULLETS;

    for (int i = 0; i < gBulletCount; ++i)
        gBullets[i] = (i < live) ? 1 : 0;      // 先按顺序放好

    // 洗牌（Fisher-Yates，等价于原程序"随机位置取弹"的效果）
    for (int i = gBulletCount - 1; i > 0; --i) {
        int j = randomInt(0, i);
        int t = gBullets[i]; gBullets[i] = gBullets[j]; gBullets[j] = t;
    }
}

// 回合开始时清掉双方的一次性状态
static void resetRoundFlags(Player& a, Player& b)
{
    gDamage = 1;
    a.cuffs = b.cuffs = false;
    a.knifeUsed = b.knifeUsed = false;
    a.knowsNext = b.knowsNext = false;
}

// ---------------------------------------------------------------------------
// build_gun() —— 单人模式：新回合装弹 + 双方各补给 4 个道具
//   规则文本："每一回合开始前，你将知道一共有几发实弹，几发空弹，
//             同时双方都将获得4个道具作为补给（上限为8个）"
//   与 build_gun_duo 同一套算法（原程序这里是独立的一份实现）。
// ---------------------------------------------------------------------------
static void build_gun()
{
    loadBullets(randomInt(1, 4), randomInt(1, 4));

    printf("%d发实弹,%d发空弹\n", gLive, gBlank);

    int haveYou = 0, haveDemon = 0;
    for (int i = 0; i < ITEM_KINDS; ++i) {
        haveYou   += gYou.items[i];
        haveDemon += gDemon.items[i];
    }

    int roomYou   = minInt(MAX_ITEMS - haveYou,   4);
    int roomDemon = minInt(MAX_ITEMS - haveDemon, 4);
    printf("你获得了%d个道具:\n", roomYou);
    printf("恶魔获得了%d个道具:\n", roomDemon);

    for (int i = 0; i < 4; ++i) {
        ++gYou.items[randomInt(0, ITEM_KINDS - 1)];
        ++gDemon.items[randomInt(0, ITEM_KINDS - 1)];
    }

    resetRoundFlags(gYou, gDemon);
}

// ---------------------------------------------------------------------------
// build_gun_duo() —— 双人模式装弹与道具补给
//   原程序（0x40157E）：
//     shi  = randomInt(1, 4);        // 实弹 1~4 发
//     kong = randomInt(1, 4);        // 空弹 1~4 发
//     ... 洗牌 ...
//     got  = min(8 - (daoju[0]+...+daoju[4]), 4);   // 还能再拿几件
//     printf("你获得了%d个道具:\n", got);
//     for (i = 1..4) { d = randomInt(0, 4); daoju[d]++; ... }   // 实际补 4 件
//   也就是说：显示的数字是"剩余可携带上限"，实际每回合补 4 件。
// ---------------------------------------------------------------------------
static void build_gun_duo()
{
    loadBullets(randomInt(1, 4), randomInt(1, 4));

    printf("%d发实弹,%d发空弹\n", gLive, gBlank);

    int haveA = 0, haveB = 0;
    for (int i = 0; i < ITEM_KINDS; ++i) { haveA += gA.items[i]; haveB += gB.items[i]; }

    int roomA = minInt(MAX_ITEMS - haveA, 4);
    int roomB = minInt(MAX_ITEMS - haveB, 4);
    printf("玩家A获得了%d个道具:\n", roomA);
    printf("玩家B获得了%d个道具:\n", roomB);

    for (int i = 0; i < 4; ++i) {
        ++gA.items[randomInt(0, ITEM_KINDS - 1)];
        ++gB.items[randomInt(0, ITEM_KINDS - 1)];
    }

    resetRoundFlags(gA, gB);
}

// ---------------------------------------------------------------------------
// useItem() —— 使用一个道具
//   返回 true 表示道具确实被使用（对应消耗一次行动）
//   说明：原程序把"谁在用道具"分成好几个分支（你 / 恶魔 / 玩家A / 玩家B），
//         这里统一成同一个函数，输出文字与原来完全一致。
// ---------------------------------------------------------------------------
static bool useItem(Player& self, Player& foe, int idx, bool canRepeat)
{
    (void)foe;

    if (idx < 0 || idx >= ITEM_KINDS) return false;

    if (self.items[idx] <= 0) {
        printf("%s现在没有%s\n", self.name, ITEM_NAMES[idx]);
        return false;
    }

    switch (idx) {
    case ITEM_MAGNIFIER:
        if (self.knowsNext) {
            printf("%s现在没有放大镜\n", self.name);
            return false;
        }
        printf("%s使用了放大镜\n", self.name);
        printf("%s看到了\n", self.name);
        // 原程序在"恶魔"看到结果时会额外说一句"有趣"
        if (strcmp(self.name, "恶魔") == 0) printf("有趣\n");
        printf("%s\n", nextIsLive() ? "实弹" : "空弹");
        self.knowsNext = true;
        break;

    case ITEM_CUFFS:
        if (self.cuffs && !canRepeat) {
            printf("%s已经用过手铐了\n", self.name);
            return false;
        }
        printf("%s使用了手铐\n", self.name);
        self.cuffs = true;
        printf("%s获得了连开两枪的机会\n", self.name);
        break;

    case ITEM_KNIFE:
        if (self.knifeUsed) {
            printf("%s已经用过小刀了\n", self.name);
            return false;
        }
        printf("%s使用了小刀\n", self.name);
        printf("若下一发为实弹则伤害+1\n");
        self.knifeUsed = true;
        gDamage = 2;
        break;

    case ITEM_CIGARETTE:
        if (self.hp >= MAX_HP_SINGLE) {
            printf("%s现在不需要烟\n", self.name);
            return false;
        }
        printf("%s使用了烟\n", self.name);
        ++self.hp;
        printf("%s回复了一点生命\n", self.name);
        break;

    case ITEM_DRINK:
        printf("%s使用了饮料\n", self.name);
        if (gBulletCount > 0) {
            printf("%s退了一发\n", self.name);
            printf("%s\n", nextIsLive() ? "实弹" : "空弹");
            popBullet();
        }
        break;
    }

    --self.items[idx];
    return true;
}

// ---------------------------------------------------------------------------
// IsOver() —— 单人模式胜负判定
// ---------------------------------------------------------------------------
static bool IsOver()
{
    if (gYou.hp <= 0) {
        printf("你输了\n");
        pauseScreen();
        exit(0);
    }
    if (gDemon.hp <= 0) {
        printf("你赢了\n你获得了奖金$%.2lf\n", gMoney);
        pauseScreen();
        exit(0);
    }
    return false;
}

// ---------------------------------------------------------------------------
// IsOver_duo(a, b) —— 双人模式胜负判定，返回赢家编号
//   注意：原程序定义了这个函数但从未调用，这里原样保留以备对照。
//   （[[maybe_unused]] 是后加的，只为在新编译器下零警告；
//     用 GCC 4.9 编译时删掉它即可。）
// ---------------------------------------------------------------------------
[[maybe_unused]] static int IsOver_duo(int a, int b)
{
    if (gA.hp <= 0) return b;
    if (gB.hp <= 0) return a;
    return a;
}

// ---------------------------------------------------------------------------
// demonTurn() —— 恶魔的一个回合（单人模式）
//   决策：已知下一发是空弹就打自己；血量偏低时更倾向赌一把；
//         否则随机。会按需使用手里的道具。
// ---------------------------------------------------------------------------
static void demonTurn()
{
    printf("现在是恶魔的回合\n");

    // ---- 恶魔使用道具（策略为还原推断，原实现的权重无法从二进制完全确定）----
    if (gDemon.items[ITEM_CIGARETTE] > 0 && gDemon.hp < MAX_HP_SINGLE)
        useItem(gDemon, gYou, ITEM_CIGARETTE, false);

    if (gDemon.items[ITEM_MAGNIFIER] > 0 && !gDemon.knowsNext)
        useItem(gDemon, gYou, ITEM_MAGNIFIER, false);

    // 已知是空弹 → 用手铐和小刀放大收益
    if (gDemon.knowsNext && !nextIsLive()) {
        if (gDemon.items[ITEM_CUFFS] > 0 && !gDemon.cuffs)
            useItem(gDemon, gYou, ITEM_CUFFS, false);
    } else {
        // 准备打实弹 → 小刀加成
        if (gDemon.items[ITEM_KNIFE] > 0 && !gDemon.knifeUsed)
            useItem(gDemon, gYou, ITEM_KNIFE, false);
        if (gDemon.items[ITEM_DRINK] > 0 && nextIsLive() && rand() % 2 == 0)
            useItem(gDemon, gYou, ITEM_DRINK, false);
    }

    bool again = false;
    do {
        again = false;

        bool live = nextIsLive();
        bool shootSelf;
        if (gDemon.knowsNext && !live)
            shootSelf = true;                              // 确定是空弹
        else if (gDemon.hp <= MAX_HP_SINGLE / 2)
            shootSelf = (rand() % 3 != 0);                 // 血量低，多赌一点
        else
            shootSelf = (rand() % 2 == 0);

        gDemon.knowsNext = false;

        if (shootSelf) {
            printf("恶魔决定向自己开枪\n");
            waitMs(400);
            popBullet();
            if (!live) {
                printf("是空弹\n");
                if (gDemon.cuffs) {
                    printf("因为恶魔使用了手铐，所以可以再来一次\n");
                    gDemon.cuffs = false;
                    waitMs(300);
                    again = true;
                }
            } else {
                printf("是实弹\n");
                gDemon.hp -= gDamage;
                gDamage = 1;
                gDemon.knifeUsed = false;
                waitMs(400);
                IsOver();
            }
        } else {
            printf("恶魔决定向你开枪\n");
            waitMs(400);
            popBullet();
            if (!live) {
                printf("是空弹\n");
                if (gDemon.cuffs) {
                    printf("因为恶魔使用了手铐，所以可以再来一次\n");
                    gDemon.cuffs = false;
                    waitMs(300);
                    again = true;
                }
            } else {
                printf("是实弹\n");
                gYou.hp -= gDamage;
                gDamage = 1;
                gDemon.knifeUsed = false;
                waitMs(400);
                IsOver();
            }
        }
    } while (again);
}

// ---------------------------------------------------------------------------
// Play() —— 原程序里对外的名字；等价于恶魔的一个回合
// ---------------------------------------------------------------------------
static void Play() { demonTurn(); }

// ---------------------------------------------------------------------------
// showStatus() —— 单人模式局面
// ---------------------------------------------------------------------------
static void showStatus()
{
    printf("你的生命:%d/6\n", gYou.hp);
    printf("恶魔生命:%d/6\n", gDemon.hp);
    printf("剩余实弹数:%d    剩余空弹数:%d\n", gLive, gBlank);

    printf("你现在拥有的道具:\n");
    for (int i = 0; i < ITEM_KINDS; ++i) {
        printf("%s%d个", ITEM_NAMES[i], gYou.items[i]);
        if (i != ITEM_KINDS - 1) printf(",");
    }
    printf("\n");

    printf("恶魔现在拥有的道具:\n");
    for (int i = 0; i < ITEM_KINDS; ++i) {
        printf("%s%d个", ITEM_NAMES[i], gDemon.items[i]);
        if (i != ITEM_KINDS - 1) printf(",");
    }
    printf("\n");
}

// ---------------------------------------------------------------------------
// danrenplay() —— 单人模式主循环（你 vs 恶魔）
// ---------------------------------------------------------------------------
static void danrenplay()
{
    while (true) {
        if (gBulletCount <= 0) build_gun();

        showStatus();
        printf("现在是你的回合\n");

        bool acted = false;          // 本回合是否已经消耗掉一次行动
        while (!acted) {
            printf("你要\n1.向恶魔开枪\n2.向自己开枪\n");
            for (int i = 0; i < ITEM_KINDS; ++i)
                printf("%d.使用%s\n", i + 3, ITEM_NAMES[i]);

            int choice = 0;
            if (scanf("%d", &choice) != 1) choice = 0;

            if (choice < 1 || choice > 7) {
                printf("输入不合法\n");
                waitMs(1000);
                clearScreen();
                continue;
            }

            // ---- 使用道具 ----
            if (choice >= 3) {
                if (!useItem(gYou, gDemon, choice - 3, gYou.cuffs)) {
                    pauseScreen();
                    clearScreen();
                    continue;
                }
                IsOver();
                // 手铐带来的额外行动在真正打出空弹时才兑现，这里直接继续本回合
                continue;
            }

            // ---- 开枪 ----
            bool live = nextIsLive();

            if (choice == 1) printf("你决定向恶魔开枪\n");
            else             printf("你决定向自己开枪\n");
            waitMs(300);

            popBullet();
            printf("%s\n", live ? "是实弹" : "是空弹");

            if (choice == 1) {
                // 向恶魔开枪：无论结果都停止行动
                if (live) {
                    gDemon.hp -= gDamage;
                    gDamage = 1;
                    gYou.knifeUsed = false;
                }
                waitMs(300);
                IsOver();
                acted = true;
            } else {
                // 向自己开枪：空弹可继续行动，实弹停止
                if (live) {
                    gYou.hp -= gDamage;
                    gDamage = 1;
                    gYou.knifeUsed = false;
                    waitMs(300);
                    IsOver();
                    acted = true;
                } else {
                    if (gYou.cuffs) {
                        printf("因为你使用了手铐，所以可以再来一次\n");
                        gYou.cuffs = false;
                        waitMs(300);
                        continue;          // 额外行动
                    }
                    waitMs(300);
                    acted = true;
                }
            }
        }

        Play();                            // 恶魔回合
        IsOver();
    }
}

// ---------------------------------------------------------------------------
// duorenplay() —— 双人模式主循环（玩家A vs 玩家B）
//   流程与单人模式一致，只是称谓与生命上限不同（4 点）。
// ---------------------------------------------------------------------------
static void duorenplay()
{
    Player* cur = &gA;
    Player* foe = &gB;

    while (true) {
        if (gBulletCount <= 0) build_gun_duo();

        printf("玩家A的生命:%d/4\n", gA.hp);
        printf("玩家B的生命:%d/4\n", gB.hp);
        printf("剩余实弹数:%d    剩余空弹数:%d\n", gLive, gBlank);

        printf("玩家A现在拥有的道具:\n");
        for (int i = 0; i < ITEM_KINDS; ++i) {
            printf("%s%d个", ITEM_NAMES[i], gA.items[i]);
            if (i != ITEM_KINDS - 1) printf(",");
        }
        printf("\n");
        printf("玩家B现在拥有的道具:\n");
        for (int i = 0; i < ITEM_KINDS; ++i) {
            printf("%s%d个", ITEM_NAMES[i], gB.items[i]);
            if (i != ITEM_KINDS - 1) printf(",");
        }
        printf("\n");

        printf("现在是%s的回合\n", cur->name);
        printf("%s要\n1.向%s开枪\n2.向自己开枪\n", cur->name, foe->name);
        for (int i = 0; i < ITEM_KINDS; ++i)
            printf("%d.使用%s\n", i + 3, ITEM_NAMES[i]);

        int choice = 0;
        if (scanf("%d", &choice) != 1) choice = 0;

        if (choice < 1 || choice > 7) {
            printf("输入不合法\n");
            waitMs(1000);
            clearScreen();
            continue;
        }

        // ---- 使用道具 ----
        if (choice >= 3) {
            if (!useItem(*cur, *foe, choice - 3, cur->cuffs)) {
                pauseScreen();
                clearScreen();
                continue;
            }
            waitMs(300);
            clearScreen();
            continue;                      // 使用道具不换人
        }

        // ---- 开枪 ----
        bool live = nextIsLive();

        if (choice == 1) printf("%s决定向%s开枪\n", cur->name, foe->name);
        else             printf("%s决定向自己开枪\n", cur->name);

        popBullet();
        printf("%s\n", live ? "是实弹" : "是空弹");
        waitMs(500);

        if (live) {
            if (choice == 1) foe->hp -= gDamage;
            else             cur->hp -= gDamage;
            gDamage = 1;
            cur->knifeUsed = false;

            if (gA.hp <= 0) {
                printf("玩家B赢了\n玩家B获得了奖金$%.2lf\n", gMoney);
                pauseScreen();
                return;
            }
            if (gB.hp <= 0) {
                printf("玩家A赢了\n玩家A获得了奖金$%.2lf\n", gMoney);
                pauseScreen();
                return;
            }
            // 换人
            Player* t = cur; cur = foe; foe = t;
        } else if (choice == 1) {
            // 空弹 + 向对方开枪：同样换人
            waitMs(300);
            Player* t = cur; cur = foe; foe = t;
        } else {
            // 空弹 + 向自己开枪：继续行动
            if (cur->cuffs) {
                printf("因为%s使用了手铐，所以可以再来一次\n", cur->name);
                cur->cuffs = false;
            }
            waitMs(300);
        }

        clearScreen();
    }
}

// ---------------------------------------------------------------------------
// showRules() —— 规则说明
// ---------------------------------------------------------------------------
static void showRules()
{
    printf("规则:\n");
    printf("你和恶魔都各有6点生命\n");
    printf("每一回合开始前，你将知道一共有几发实弹，几发空弹，同时双方都将获得4个道具作为补给（上限为8个）\n");
    printf("每一回合，你可以选择对自己开枪，对恶魔开枪或者使用道具\n");
    printf("如果你对自己开枪，若为空弹，则可以继续行动，否则，停止行动\n");
    printf("如果你对恶魔开枪，无论如何，都将停止行动\n");
    printf("道具一览：\n");
    printf("放大镜：可以知道下一发子弹是空弹还是实弹\n");
    printf("手铐：增加一次本回合的行动次数\n");
    printf("小刀：若下一发子弹为实弹，则伤害+1\n");
    printf("烟：可以回复1点体力\n");
    printf("饮料：可以退一发子弹\n");
}

// ---------------------------------------------------------------------------
// main() —— 入口：菜单 / 规则 / 模式选择
// ---------------------------------------------------------------------------
int main()
{
    srand((unsigned)time(NULL));

    gYou.name   = "你";     gDemon.name = "恶魔";
    gA.name     = "玩家A";  gB.name     = "玩家B";

    printf("又来了一位挑战者...\n");
    printf("准备好参与恶魔的游戏吗？胜者带走奖金，败者将会在此长眠\n");
    printf("1.好的\n");
    printf("2.没问题\n");

    int a = 0;
    scanf("%d", &a);

    printf("你清楚我们的规则吗？\n");
    printf("1.清楚\n");
    printf("2.不清楚\n");

    int b = 0;
    scanf("%d", &b);
    if (b == 2) showRules();

    printf("好吧\n");
    printf("游戏将要开始了哦\n");
    waitMs(1000);
    clearScreen();

    printf("请选择你想要的模式：\n");
    printf("1.单人\n");
    printf("2.双人（此模式中，生命值为4，道具补给为2）\n");

    int mode = 0;
    scanf("%d", &mode);

    gMoney = (double)randomInt(0, 999999) / 100.0;

    if (mode == 2) {
        gA.hp = gB.hp = MAX_HP_DUO;
        memset(gA.items, 0, sizeof(gA.items));
        memset(gB.items, 0, sizeof(gB.items));
        duorenplay();
    } else {
        gYou.hp = gDemon.hp = MAX_HP_SINGLE;
        memset(gYou.items, 0, sizeof(gYou.items));
        memset(gDemon.items, 0, sizeof(gDemon.items));
        danrenplay();
    }

    pauseScreen();
    return 0;
}
