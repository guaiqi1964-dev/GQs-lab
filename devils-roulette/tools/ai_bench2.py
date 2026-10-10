# -*- coding: utf-8 -*-
"""Fairer headless benchmark: the player also gets the magnifier, so we compare
"good player" vs "old random demon" and "good player" vs "new sharp demon".
Uses the item model that matters most (放大镜), which is what makes the demon
strong in the first place."""
import random, functools

HP = 6
P = 0.35          # how often each side finds a magnifier in its supply
MAXN = 8

@functools.lru_cache(maxsize=None)
def solve(l, b, mh, oh, dmg):
    if mh <= 0: return 0.0
    if oh <= 0: return 1.0
    if l + b == 0: return 0.5
    t = l + b
    sf = ((l * (1.0 if oh - dmg <= 0 else 1.0 - solve(l-1, b, mh, oh-dmg, 1)) +
           b * (1.0 - solve(l, b-1, mh, oh, 1))) / t) if t else 0.0
    ss = ((b * solve(l, b-1, mh, oh, dmg) +
           l * (0.0 if mh - dmg <= 0 else 1.0 - solve(l-1, b, mh-dmg, oh, 1))) / t) if t else 0.0
    return max(sf, ss)

def smart_pick(l, b, mh, oh, dmg):
    """True = shoot self, using exact probabilities (0 if unknowable)."""
    if l == 0: return True
    if b == 0: return False
    t = l + b
    sf = ((l * (1.0 if oh - dmg <= 0 else 1.0 - solve(l-1, b, mh, oh-dmg, 1)) +
           b * (1.0 - solve(l, b-1, mh, oh, 1))) / t)
    ss = ((b * solve(l, b-1, mh, oh, dmg) +
           l * (0.0 if mh - dmg <= 0 else 1.0 - solve(l-1, b, mh-dmg, oh, 1))) / t)
    return ss > sf

def round_sim(player_uses_magnifier, demon_mode, rng):
    """demon_mode: 'old' (random 50/50) or 'sharp' (probability) or 'casual'."""
    l, b = rng.randint(1,4), rng.randint(1,4)
    queue = [1]*l + [0]*b; rng.shuffle(queue)
    php, dhp, dmg = HP, HP, 1
    p_mag = rng.random() < P          # player holds a magnifier
    d_mag = rng.random() < P
    p_knows = d_knows = False
    turn = 'player'
    for _ in range(600):
        if php <= 0 or dhp <= 0: break
        if not queue:
            l, b = rng.randint(1,4), rng.randint(1,4)
            queue = [1]*l + [0]*b; rng.shuffle(queue)
            dmg = 1; p_knows = d_knows = False
        live = queue[0] == 1
        cur_l = sum(queue); cur_b = len(queue) - cur_l

        if turn == 'player':
            known = p_knows
            p_knows = False
            if player_uses_magnifier and not known and p_mag and len(queue) > 1:
                p_mag = False; known = True
            if known:
                act_self = not live
            else:
                act_self = smart_pick(cur_l, cur_b, php, dhp, dmg)
            queue.pop(0)
            if act_self:
                if live:
                    php -= dmg; dmg = 1; turn = 'demon'
                # blank: keep turn
            else:
                if live:
                    dhp -= dmg; dmg = 1
                turn = 'demon'
        else:
            known = d_knows
            d_knows = False
            if demon_mode != 'old' and not known and d_mag and len(queue) > 1:
                d_mag = False; known = True
            if demon_mode == 'old':
                act_self = rng.random() < 0.5
            elif known:
                act_self = not live
            elif demon_mode == 'casual':
                act_self = (rng.random() < 0.5) if dhp < php else False
            else:
                act_self = smart_pick(cur_l, cur_b, dhp, php, dmg)
            queue.pop(0)
            if act_self:
                if live:
                    dhp -= dmg; dmg = 1; turn = 'player'
            else:
                if live:
                    php -= dmg; dmg = 1
                turn = 'player'
    if dhp <= 0: return 'player'
    if php <= 0: return 'demon'
    return 'draw'

rng = random.Random(2024)
N = 4000
print('%-30s %-22s %8s %8s' % ('player', 'demon', 'player%', 'demon%'))
print('-'*74)
for pname, pmag in [('no items (baseline)', False), ('uses magnifier', True)]:
    for dname, dmode in [('old: random 50/50', 'old'),
                         ('casual heuristic', 'casual'),
                         ('sharp minimax', 'sharp')]:
        w = d = 0
        for _ in range(N):
            r = round_sim(pmag, dmode, rng)
            if r == 'player': w += 1
            elif r == 'demon': d += 1
        print('%-30s %-22s %7.1f%% %7.1f%%' % (pname, dname, 100.0*w/N, 100.0*d/N))
