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
    # the screen renders not ported yet (the screen dispatcher 0x40f1a0's arms, terrain info, the frame driver's HUD
    # draws, the MP debug HUD, the movie / screen transition draw) — verifiable since the gfx layer runs on retail's
    # layout (docs/findings/render-under-rhost.md)
    "render": _h("40c725 40cd58 410c71 410f4b 4120e0 412421 412795 41298a 41438c 414819 414f68 41515c 4152ea 41608b "
                 "417aef 417bd3 417cef 4192b1 419354 419789 4198aa 41a166 41a639 41a844 41b19d 41c996 41d299 41da2f "
                 "41e1dd 41ff75 420316 421f14 4224e7 422d22 422ff1 423241 4233f7 423530 423739 423ba4 42476b 4248a3 "
                 "424992 42e060 438e3b 44098f 4788c6"),
    # the last reached leftovers: palette fades, the cursor, MP timeouts, empty stubs, window probes
    "misc": _h("45242b 497879 49a3e6 4b0cb4 4b0e03 4b12fb 4b1897 4b18f8 4b1cf3 4b1dd5 4b209a 4b25fc 4b26f8 4b28cd "
               "4b3571 4bbe51 4bbea7 4bbf15"),
    # skirmish mode (reached by skirmish-tour): the setup pages, the .skr list, the Battle Master ratings, score.dat
    "skirmish": _h("420147 42051c 4207c3 4209c1 420d40 420de4 421005 421231 42130f 42150b 421707 421d09 425314 425487 42aa90 "
                 "42ab15 42ab82 42ac0c 42b780 42b7f7 42b919 42b9c4 42ba40 42bf46 42c476 42c4e2 42c5ad 42c5fe 42c64f 42cc4a "
                 "42d216 42d2dc 42d389 42d4aa 42d857"),
    # what the UI tours reach besides: the labor model (worker drags), the kingdom overview, the armoury's weapon
    # assignment, custom-setup options, the county panels' rarer arms
    "ui2": _h("401c5b 408ea4 408f1a 413526 41424a 414e06 41543f 4154ea 41cb3a 41ef42 433dbd 433deb 434174 4343f8 43441c "
                 "434787 4347c7 43484a 434888 4348c6 434904 434d33 434d60 434fa1 434fd5 43505a 4350a1 43600c 438b02 438bec "
                 "439541 43958a 4399b0 439b52 439cc2 439edb 439f6a 43a950 43a997 43ab76 43c77a 43c885 43c910 43d5b7 43d649 "
                 "43d6a7 43d6c3 43d78b 43d7a7 43d882 43d8b9 43d929 43d9cd 43da9e 43daf3 43dbac 43dc1d 43dd83 43ddf4 43f24b "
                 "43f352 43f484 44ff4a 450000 450639 46d421 479276 47935b 498dcb 498fb3 4adcf6 4af70c 4b14ff 4b3994 4b7250 "
                 "4b7585"),
    # what round-2 scenarios reach besides: the options menu's handlers, the noble faces' insult / alliance buttons, the
    # army split (Divide), 0x43bbe7, 0x4477e7, 0x40df62
    "ui3": _h("40df62 434556 4345d0 434693 43480c 434942 4349a4 434a29 434a9a 434ad5 434b10 434e9e 434ec9 4361da "
              "436229 4362ca 4378b3 437afb 437d65 437e9e 437fd7 43bbe7 4477e7"),
    # round 3 — the 0x401000..0x42ff0f band the corpus reaches whose old ports are not on retail's layout: strutil, text input, font metrics, localization, the menu hotspots, config defaults, the screen dispatch 0x40f1a0, the labor / rankings / armoury / send-supplies / diplomacy panels, the HUD chrome, audio init, the category buckets, the camera
    "ui4": _h("401000 401085 4010da 401136 4011b0 4011ea 401246 4012a2 401303 401333 4013bc 4014f0 4015b9 401666 "
          "4017bf 40187a 4018d7 401984 401a20 401c70 401c93 401ca3 401cbc 401cda 401cfc 401d11 401d26 401dc8 "
          "401f49 401f8f 402009 4020ae 40210c 4036f9 4037ee 40489e 40490d 404b2c 404d6b 404d96 404dc1 404e03 "
          "404e4b 404ead 404f4c 408fcb 40ccfa 40da1e 40e3ee 40e5e7 40e6f2 40e7e4 40e890 40e8bb 40ecc4 40f1a0 "
          "40f5fd 410a5d 412b33 412e6b 413155 413590 413b30 4140f3 414220 41593b 415bdc 415e42 415fb7 41789b "
          "417960 4180f6 4184c6 418e2d 419c78 41ad5d 41aea2 41e61d 41fdd6 425b10 425ded 426d11 427d19 427ed0 "
          "428471 42847c 4290d0 429153 4291b3 42925e 4298c1 429b1d 429ba4 42a15f 42e5c0 42eb7c 42ec91 "
          "42f1df 42f306"),
    # round 3 — the 0x42ff10..0x465485 band: the in-game input dispatch 0x42ff10, edge scroll, the title menu actions, the armoury zones, the army select, the county rations, end turn, the scenario hint, delayed actions, coalitions, realm development, the army baseline seed, the dirty-rect marks
    "ctl": _h("42ff10 431f59 432221 432640 432746 43278b 432967 4329ec 432b05 432cc8 432ee6 432fab 433155 433461 "
          "4335f0 4337b4 433905 433998 433a23 433ba2 433ce4 43400b 4342f3 434346 434e1f 4351c4 435211 43524e "
          "43582a 4358b0 43593a 4359bc 435a0c 435a61 435ae8 435b4d 435c89 435cbf 43611b 436141 436a88 4371ce "
          "43723a 4398f5 439df0 43a123 43a1e9 43a2b2 43a5a9 43a846 43a8f2 43a922 43a939 43aa32 43aad5 43ac23 "
          "43ad25 43b04c 43caf4 43cc56 43ce1a 43d309 43de70 43e9e2 4472ce 448440 448648 449d6e 44ae51 44b042 "
          "44b302 44b391 44b4d0 44ba35 44bfd5 44c093 44c6c4 44d281 44d303 450fb4 451150 4514f8 45183a 451f1a "
          "451fcc 452160 452306 4523e6 45240a 452436 452489 4524dc 452628 45270a 452775 4527a6 4651b9 46521f"),
    # round 3 — the 0x465486..0x47309d band: the strategic map's territory build and cell edits, the map queries, the path field fill / trace, the greedy path search, the event queue, the event-popup step
    "terr": _h("465486 465622 465761 46733c 4673b8 467532 4676e0 467770 46797d 467a36 467ca6 467e2c 467f2e 467fd1 "
          "468121 46841c 4684c6 468c41 468d4f 468e61 46902a 46921d 46942c 46958f 46965a 4697cd 46988d 469a9c "
          "469d21 46a037 46ac22 46c080 46c147 46c2de 46c5b8 46c6ba 46cfbd 46d2a3 46d5fe 46d7f4 46d9da 46da4b "
          "46dbf5 46dd34 46de05 46de94 46df51 46dfd5 46e1b0 46ea28 46ea61 46ead0 46eb09 46f1dc 46f2ae 46f3cd "
          "46f68e 46f700 46ff43 4701ac 470678 4707be 4708d0 472392 47265d 472875 472ad0 472ae0 472b40 472bc5 "
          "472e46"),
    # round 3 — the 0x47309e..0x4a57ab band: the popup scroll render 0x47309e, the help hints, game init (seat shuffle, realms setup, NPC schedule), the turn substate 0, the rankings recompute, the roster, the AI's realm passes, diplomacy reconcile
    "turn": _h("47309e 476710 476768 47685d 476a5d 476aa7 476da9 476e21 47ed34 4976a1 497ced 497e65 498270 4984dc "
          "499034 4997bf 499859 499a1c 499dc3 499e5d 49a010 49a453 49aa0e 49b35e 49b6d3 49b762 49bae9 49bbe8 "
          "49bc1d 49bc5f 49bd99 49ca32 49caaa 49ce51 49f12f 49f431 49f73f 49f850 49fcc5 49fda5 4a01ea 4a0649 "
          "4a1241 4a1774 4a1847 4a1a54 4a1c53 4a1e6c 4a24d1 4a2584 4a277d 4a29b2 4a2cd6 4a2f61 4a30f6 4a3475 "
          "4a38db 4a3c67 4a4052 4a440f 4a4a3f 4a545f"),
    # round 3 — the 0x4a57ac.. band: the AI's army orders, the siege board, the move-confirm hover, spawn / mercenary / unit sweeps, the load pump, save write / load, the io path helpers, the input state, the wndproc, the frame tick 0x4b99c0, the diplomacy refresh, the title input, the sim sub-steps
    "host": _h("4a57ac 4a58f4 4a5b1f 4a5f0a 4a60b9 4a6270 4a64ca 4a65a3 4a6735 4a7e0a 4a7eb5 4a8426 4a8d9c 4a8e0b "
          "4a91ba 4a9252 4a9372 4a960f 4a975d 4a983b 4a98fe 4a9eb1 4a9f18 4a9f72 4aabd8 4ac7f3 4ac904 4acb54 "
          "4ace5e 4ad0e8 4ad16c 4ad230 4ad316 4ad39a 4ad9f0 4ade93 4adfe6 4ae310 4ae74d 4ae8a9 4aef7e 4af502 "
          "4af54a 4af62c 4af675 4af8d9 4b0ab5 4b1867 4b18e3 4b191e 4b1de0 4b1deb 4b20ed 4b25b0 4b27bf 4b284e "
          "4b2922 4b29be 4b35c1 4b3768 4b3e51 4b495a 4b99c0 4ba26e 4ba9c8 4bb8c0 4bb978 4bbbe3 4bbc80 4bc020"),
    # what coverage round 3's scenarios newly reach (custom-strong, siege-castles, movie-skip, custom-garrison): the movie
    # skip, the load box's Cancel, the paid ally answer, the siege-preparation "-", the divide placement search, the
    # castle damage read-back on the field, the AI campaign check / gift / plea for help, the castle scenario write / read
    "cov3": _h("42df30 434308 4367ff 43b741 46d130 4917dd 49ff71 4a1b29 4a3551 4ae968 4aea70"),
    # the game's exit (scenario game-quit): WM_DESTROY's movie / sound teardown, WinMain's exit (lords2.inf, the
    # sprite frees), the DirectDraw / DirectSound / DirectPlay releases, the exit log
    "quit": _h("42dfe2 425d35 4ae15f 49767d 42eaa6 4b7180 4b2636 43f43e"),
    # retail seams the battlefield calls that stay out of scope (audio streams, sprite loads): stubs only
    "extern": _h("43f304 477b2f 4987b7 499d97"),
    # --- multiplayer (2026-11-14, docs/findings/multiplayer-rhost.md): what the mp-* scenarios newly reach ---
    # the MP message layer: the receive dispatcher 0x43eac1, the payload builders (table 0x4d57f0) and receivers
    "mpmsg": _h("43e330 43e433 43e50d 43e5c1 43e98b 43eac1 43f01d 43f4b7 43f8ff 43fd2a 43fed2 440038 44015a 4401ba  "
               "4401f6 440231 4407e5 440dd2 441066 4410bd 44121a 4412cc 4413a4 44164d 441905 4419f9 441b4e 441b86  "
               "441d2a 441e1f 441f2a 441f50 441f76 442014 4420ad 4421c3 442309 442361 4423b9 44240a 442448 4424d6 "
               "4425bf 4427d1 4429f2 442a25 442afd 442b08 442bad 442c01 442c3f 442c82 442cb9 442cd6"),
    "mpmsg2": _h("4431ef 443286 4433d9 44341b 44348c 4434eb 443748 4437aa 44381d 4438c3 443990 443a2d 443ad5 443b8d  "
               "443c2f 443ca6 443d0a 443dba 444060 444100 444191 44419c 4441d7 444280 4443fb 444415 4449a0 4449f1  "
               "444a2f 444a9b 444d04 444d68 4458e5 445960 445c99 445d23 445e6f 445ea7 445f43 445f80 4462c5 4462df  "
               "446e21 446f84 447026 447136 4476e4 447740 4479de 4479e9 447b8e 447c68 447c99 447d22 447db6 44800e "
               "448339 448422"),
    # the SIGS transport class 0x4b61b0.., the monitor thread, the provider dialog, the MP title pages, MP popups
    "mpnet": _h("41f3e9 41f4a1 41f77a 43306a 433551 46f2f1 475e07 47f937 49b973 49ca60 4b3d5d 4b61b0 4b62ea 4b63d0  "
               "4b64d8 4b6754 4b6ae5 4b6b71 4b6c59 4b6c82 4b6ccb 4b6cee 4b6d19 4b6d44 4b6dc5 4b6de6 4b6e1b 4b6e8e "
               "4b7052 4b7514 4b7a9a 4b7f78 4b90fb 4bbf89 4bbfd2"),
    # round 2 (mp-trio / mp-skirmish / mp-siege / mp-five / mp-dp / mp-leave): what those newly reach
    "mpmsg3": _h("43e6a2 43e8f4 44255f 442579 442add 442ae8 442d00 442d60 442dad 442e0d 442e5a 442eec 443043 443083 "
               "4430d5 443143 44334e 44338b 44356d 443587 4435ce 44363b 443e51 443f09 443fab 44400f 444346 4443aa "
               "44462f 4446a6 44470a 44475c 4447ab 44480f 444860 4448c4 444b07 444b17 444b27 444bd0 444c66 444cb5 "
               "444e3f 444f96 445063 4451cc 445458 4454a9 44562e 4456c1 4457a1 445851 445d97 445e0e 4460f8 446171 "
               "4461f4 44622c 44637e 446389 4463d9 4464e4 446683 44678d 446ba0 446bba 446bec 446c1f 447a59 447ac5 "
               "447d72 447d9d 447e0b 447f82 448076 448137 44825d 448276 44828f 44829f 4482e9 448308 4483d1 "
               "4436bb 4436f9 444915 444948 4469bd 4469ec 446ab8 446b0f"),   # + mp-labor / mp-skrpick (2026-11-15)
    "mpnet2": _h("40ec2c 420630 42d40b 43338e 43695d 476488 479e0c 49792e 4b64a3 4b6a15 4b6ab6 4b6ef7 4b743b 4b756b "
               "4b79e3 4b7a7c 4b8243 4b82bf 4b8335 4b8439 4b85b7 4b8b87 4b92a0"),
}
FAMILY = {"io": "rl", "glue": "rl", "render": "rl", "misc": "rl", "skirmish": "rl", "ui2": "rl", "ui3": "rl", "ui4": "rl", "ctl": "rl", "terr": "rl", "turn": "rl", "host": "rl", "quit": "rl", "cov3": "rl", "mpmsg": "rl", "mpmsg2": "rl", "mpnet": "rl", "mpmsg3": "rl", "mpnet2": "rl"}   # default "bf"
OWNER = {}
for _p, _vs in PARTS.items():
    for _v in _vs:
        assert _v not in OWNER, hex(_v)
        OWNER[_v] = _p
