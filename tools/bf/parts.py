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
    # the battlefield's input (screens 0x28..0x2a: select, order, the control panel) and its HUD panels
    "input": _h("4238b8 4239d5 43b9a1 43ba29 43bb68 43bbd7 43bd02 43bd67 43bdcd 43bf07 43c247 43c2a9 43c4c6 43c55c "
                "43c57d 43c634 4784ca 478987 478f0b 479a71 479b58 479cf7 47a76d 47f76d 499cb1"),
    # --- family 'rl' (retail-layout ports outside the battlefield: rl_<va>, src/rl_<part>.c) ---
    # the platform layer: asset / file loaders, the WAV cues and music streams, movies + screen transitions, the
    # debug log, the sprite / palette loads (reached by every scenario)
    "io": _h("41b013 425e4b 426216 4262eb 426360 4263ad 426529 426534 426b92 426dd5 427990 427c9b 42d91b 42da18 "
             "42dbc7 42e298 42e320 42e362 42f58c 433cc1 451f70 45280b 46a101 46ea9a 46f21f 46f267 497500 4975d3 "
             "4978ad 497a34 4983b7 4990f9 49915e 499261 499366 499619 49964c 4996c8 499aca 499d7d 49c995 4ae1b8 "
             "4ae2ab 4ae2bd 4aecf0 4aee3e 4af116 4af312 4af767 4af841 4afab9 4afe3a 4b049b 4b0aaa 4b11ce 4b2575 "
             "4b36c0 4b3714 4b37bc 4b3940 4b3acd 4b3b92 4b8c3a 4b92e0 4b95ba 4b9652 4b97f9 4b984f 4c0580 4c05a0"),
    # the UI's logic glue: the strategic screen's button / router actions, the per-screen input sub-handlers of
    # 0x42ff10, the frame driver's pre-tick steps, the MP message sender's SP arm
    "glue": _h("409429 40ae12 40c8d1 40c928 40dd92 40deca 40dfcd 40e00a 40e099 40e761 40f10f 41423f 42a8d1 42a9ab "
               "42d179 42e26f 42f148 4323fe 432443 43253a 432893 43292d 4329a4 4330c6 433a70 433e93 433f49 4344f2 "
               "43470d 43496c 434988 434cbe 434cee 4360f2 43618b 43631e 436872 4369bd 436b22 436b59 437002 43733a "
               "43744f 4374c4 4376bb 438990 438a91 438acc 439079 439122 43933b 4393eb 43b4cb 43b593 43b681 43b7c4 "
               "43b848 43b98a 43eda0 440666 448d7e 451f8a 46c4a3 472986 472a31 475b41 475d73 47703a 4782c5 47f474 "
               "481b9a 482111 498458 49b8b0 4a82b9 4b1310"),
    # retail seams the battlefield calls that stay out of scope (audio streams, sprite loads): stubs only
    "extern": _h("43f304 477b2f 4987b7 499d97"),
}
FAMILY = {"io": "rl", "glue": "rl"}   # default "bf"
OWNER = {}
for _p, _vs in PARTS.items():
    for _v in _vs:
        assert _v not in OWNER, hex(_v)
        OWNER[_v] = _p
