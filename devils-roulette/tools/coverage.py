# -*- coding: utf-8 -*-
"""Compare string coverage of the two C++ sources against the 144 verified
game messages extracted from the original EXE."""
import re, sys, os
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

GENUINE = [
 "%d发实弹,%d发空弹","你获得了%d个道具:","恶魔获得了%d个道具:","你输了","你赢了",
 "你获得了奖金$%.2lf","你的生命:%d/6","恶魔生命:%d/6","剩余实弹数:%d    剩余空弹数:%d",
 "你现在拥有的道具:","个","恶魔现在拥有的道具:","现在是你的回合","你要",
 "1.向恶魔开枪","2.向自己开枪","%d.使用","输入不合法","你决定向恶魔开枪","是空弹",
 "因为你使用了手铐，所以可以再来一次","是实弹","你决定向自己开枪","你使用了放大镜",
 "你看到了","实弹","空弹","你现在没有放大镜","你使用了手铐","你获得了连开两枪的机会",
 "你已经用过手铐了","你现在没有手铐","你使用了小刀","若下一发为实弹则伤害+1",
 "你已经用过小刀了","你现在没有小刀","你使用了烟","你回复了一点生命","你现在不需要烟",
 "你现在没有烟","你使用了饮料","你退了一发","你现在没有饮料","恶魔决定向你开枪",
 "因为恶魔使用了手铐，所以可以再来一次","恶魔决定向自己开枪","现在是恶魔的回合",
 "恶魔使用了烟","恶魔回复了一点生命","恶魔使用了放大镜","恶魔看到了","有趣",
 "恶魔使用了小刀","恶魔使用了手铐","恶魔获得了连开两枪的机会","恶魔使用了饮料",
 "恶魔退了一发","又来了一位挑战者...","准备好参与恶魔的游戏吗？胜者带走奖金，败者将会在此长眠",
 "1.好的","2.没问题","你清楚我们的规则吗？","1.清楚","2.不清楚","规则:",
 "你和恶魔都各有6点生命","每一回合开始前，你将知道一共有几发实弹，几发空弹，同时双方都将获得4个道具作为补给（上限为8个）",
 "每一回合，你可以选择对自己开枪，对恶魔开枪或者使用道具",
 "如果你对自己开枪，若为空弹，则可以继续行动，否则，停止行动",
 "如果你对恶魔开枪，无论如何，都将停止行动","道具一览：",
 "放大镜：可以知道下一发子弹是空弹还是实弹","手铐：增加一次本回合的行动次数",
 "小刀：若下一发子弹为实弹，则伤害+1","烟：可以回复1点体力","饮料：可以退一发子弹",
 "好吧","游戏将要开始了哦","玩家B赢了","玩家B获得了奖金$%.2lf","玩家A赢了",
 "玩家A获得了奖金$%.2lf","玩家A获得了%d个道具:","玩家B获得了%d个道具:",
 "玩家A的生命:%d/4","玩家B的生命:%d/4","玩家A现在拥有的道具:","玩家B现在拥有的道具:",
 "现在是玩家A的回合","玩家A要","1.向玩家B开枪","玩家A决定向玩家B开枪",
 "因为玩家A使用了手铐，所以可以再来一次","玩家A决定向自己开枪","玩家A使用了放大镜",
 "玩家A看到了","玩家A现在没有放大镜","玩家A使用了手铐","玩家A获得了连开两枪的机会",
 "玩家A已经用过手铐了","玩家A现在没有手铐","玩家A使用了小刀","玩家A已经用过小刀了",
 "玩家A现在没有小刀","玩家A使用了烟","玩家A回复了一点生命","玩家A现在不需要烟",
 "玩家A现在没有烟","玩家A使用了饮料","玩家A退了一发","玩家A现在没有饮料",
 "现在是玩家B的回合","玩家B要","1.向玩家A开枪","玩家B决定向玩家A开枪",
 "因为玩家B使用了手铐，所以可以再来一次","玩家B决定向自己开枪","玩家B使用了放大镜",
 "玩家B看到了","玩家B现在没有放大镜","玩家B使用了手铐","玩家B获得了连开两枪的机会",
 "玩家B已经用过手铐了","玩家B现在没有手铐","玩家B使用了小刀","玩家B已经用过小刀了",
 "玩家B现在没有小刀","玩家B使用了烟","玩家B回复了一点生命","玩家B现在不需要烟",
 "玩家B现在没有烟","玩家B使用了饮料","玩家B退了一发","玩家B现在没有饮料",
 "请选择你想要的模式：","1.单人","2.双人（此模式中，生命值为4，道具补给为2）",
 "放大镜","手铐","小刀","烟","饮料","cls","pause",
]

def norm(s):
    s = s.replace('\\n', '')
    s = re.sub(r'%[-+ #0-9.]*[dioufeEgGxXsc]', '#', s)
    return re.sub(r'\s+', '', s)

def expand_source(src):
    """All literal text the program can emit, expanding %s with plausible names."""
    lits = re.findall(r'"((?:[^"\\]|\\.)*)"', src)
    out = set()
    for l in lits:
        for nm in ('你', '恶魔', '玩家A', '玩家B', ''):
            s = l.replace('\\n', '\n').replace('%s', nm)
            s = re.sub(r'%[-+ #0-9.]*[dioufeEgGxXsc]', '#', s)
            out.add(re.sub(r'\s+', '', s))
    # dynamic: name + verb pairs built from the item table / literals
    verbs = set()
    for l in lits:
        for nm in ('你', '恶魔', '玩家A', '玩家B'):
            if l.startswith(('使用了', '看到了', '回复了', '退了一发', '获得了', '现在没有',
                             '现在不需要', '已经用过', '决定向')):
                verbs.add(nm + l.replace('\\n', ''))
    out |= verbs
    return out

def report(path, label):
    src = open(path, encoding='utf-8').read()
    have = expand_source(src)
    whole = re.sub(r'\s+', '', re.sub(r'"\s*\n?\s*"', '', src))
    miss = []
    for gm in GENUINE:
        k = norm(gm)
        if k in have or k in whole:
            continue
        # also allow the message to be assembled from fragments present in source
        miss.append(gm)
    print('=== %s ===' % label)
    print('  verified game messages : %d' % len(GENUINE))
    print('  present in source      : %d' % (len(GENUINE) - len(miss)))
    print('  not literal-matched    : %d' % len(miss))
    for m in miss:
        print('      %r' % m)
    print()
    return len(miss)

a = report(r'D:\DSH\DSH workspace\_recovered\恶魔轮盘赌.cpp', 'faithful reconstruction v1')
b = report(r'D:\DSH\DSH workspace\_recovered\恶魔轮盘赌_优化版.cpp', 'optimized v2')
print('SUMMARY: v1 unmatched=%d, v2 unmatched=%d' % (a, b))
