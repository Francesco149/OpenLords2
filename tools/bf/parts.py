"""tools/bf/parts.py — the battlefield port's partitions: every unported battlefield function has ONE owner
(docs/findings/battlefield-sim-port.md). A partition = src/engine_bf_<part>.{c,h} + _pending.c +
tools/rhost/rh_hooks_bf_<part>.c."""


def _h(s):
    return [int(x, 16) for x in s.split()]


PARTS = {
    # the move order: the battlefield path search and step (FUN_0048f1dd and below)
    "move": _h("48f1dd 47095e 4710f2 471f45 4b3dcd 471718 472227 48f079 49005f 4928a3 496155 4904ec 490616 490a44 "
               "490c59 490fa2 491147 4912ec 491492 491a34 491b1f 491f17 49207e 494d08 494df9 4972f9"),
    # melee / missile strikes, damage and the impact effects (FUN_00483337, FUN_00492c8b and below)
    "strike": _h("483337 426120 4262cf 46e767 493cb9 46efbe 46f525 492c8b 46eb78 47dfe0 47da54 48551d 485675 485b47 "
                 "496f72 485861 493b61 494075 494125 4941c5 494265 4956cc"),
    # the per-unit driver, its movement-state table (DAT_004d9140) and the animation sequencers
    "unit": _h("4822ed 4825d1 4826b0 482789 482862 48293b 482a14 482aed 482bc6 482ca8 482d8a 482e6c 481abb 46ebe4 "
               "46eb3f 4861e9 4861f9 486209 486219 486229 486239 486249 486d83 4872ae 487908 487ce4 48804a 488436 "
               "488793 48895e 49459a"),
    # the unit order table (DAT_004d9170) and its private helpers
    "action": _h("482f86 482f91 4830e9 48314e 4831d8 4832ea 483a88 483ce1 483e55 483fe1 4842c6 48437e 4843bc 4849fc "
                 "484a0c 484b89 484bee 484bf9 494908 47a814 4954dd 49694f 471da6 494e3f 47dd86 4926fb 4927fa 496768 "
                 "405000 479e90 46e97a 46ebae 48143b 48a7d9 48a672 497437"),
    # the group AI driver + interactive stance table (DAT_004d91b8), the group aggregator, projectiles, fire
    "group": _h("489401 4891ad 489654 48970e 489798 489896 489981 489a62 489b8d 489f9d 48a1c9 48a38e 47fc01 4819cc "
                "48ef45 48a9c7 48a9d2 48acd2 48b02b 48b37b 48b386 48b546 48b5fc 48b7fc 48b9c1 48bac4 48bbe2 48bcfb "
                "48becd 48bff9 48c06f 48c363 48c3fe 48c4bd 48c56f 48c8af 48c974 488dfe 485bb1 46ef3c 4859e5 478952"),
    # the other two stance tables (DAT_004d91f8, DAT_004d91d0) and their helpers
    "stance": _h("48cba9 48cce3 48ce1d 48ce81 48d013 48d16e 48d412 48d6fc 48d9ce 48db84 48ddc7 48dfbb 48e097 48e234 "
                 "48e2c8 48e39c 48e4cc 48e774 48e8b8 48ea95 48ebf1 48ed95 495956 495c41 495f3d 496566 496b9f 496d76"),
    # entering the battlefield: terrain build, deployment, spawn (FUN_004778a0 and below), the per-frame camera
    "setup": _h("4778a0 477c89 47aaa3 47b8b2 47c4ba 4787a4 47cec1 47d474 47d4b8 47d5ec 47d6b0 47dc9c 47dcce 47e1dc "
                "47e230 47e263 47e387 47e52d 47e668 47e7b2 47e926 48eefd 497024 497247 49719e 47efee 46f0f0 46f13e "
                "46f18d 47f42d 47fea7 480599 480662 46e4c8 46e70e 479473 48169e 4816f9 481878 481906 4801f8 480f2e "
                "480f8b 46c40c 46c7fc 47d6fe 47d79c 47d816 42d806 47ed9b"),
    # retail seams the battlefield calls that stay out of scope (audio streams, sprite loads): stubs only
    "extern": _h("43f304 477b2f 4987b7 499d97"),
}
OWNER = {}
for _p, _vs in PARTS.items():
    for _v in _vs:
        assert _v not in OWNER, hex(_v)
        OWNER[_v] = _p
