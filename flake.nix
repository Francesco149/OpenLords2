{
  description = "OpenLords2 — open-source reimplementation of Lords of the Realm II (educational RE / preservation)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };

        # 32-bit mingw cross-compiler — Lords2.exe is a 32-bit Win32 PE,
        # MSVC 4.20 linker era (1996), DirectDraw/DirectSound/DirectPlay,
        # smackw32.dll for Smacker cutscenes. No DRM packer.
        mingw32 = pkgs.pkgsCross.mingw32.buildPackages;

        # Dear ImGui source (Trace Studio v3 native viewer — compiled into the
        # 32-bit viewer.exe with the Win32 + DX9 backends). ImGui is meant to be
        # vendored into the app, so we pin the upstream source via nixpkgs and
        # point the Makefile at $IMGUI_SRC rather than committing it. Mirrors the
        # openrecet sibling's viewer build. See project_trace_studio_v3 memory.
        imguiSrc = pkgs.imgui.src;

        # Python tooling — same shape as the OpenMare/openrecet siblings so
        # the same authoring habits transfer:
        #   - pillow / numpy / scikit-image / opencv4 for frame work
        #   - frida 17.x for the Windows-side instrumentation
        #   - construct for binary-format parsers (.dat archives, etc.)
        #   - rich for nicer CLI output
        pythonEnv = pkgs.python3.withPackages (ps: with ps; [
          pillow
          numpy
          scikit-image
          opencv4
          pytest
          pytest-xdist
          pyyaml
          construct
          rich
          frida-python
        ]);

      in {
        devShells.default = pkgs.mkShell {
          name = "openlords2-dev";

          packages = with pkgs; [
            # ── reverse engineering ───────────────────────────────────
            ghidra
            radare2
            rizin
            cutter
            imhex
            hexyl
            bvi
            file
            binutils          # nm, objdump, strings
            icoutils          # wrestool/icotool — extract PE resources

            # ── dynamic analysis / instrumentation ────────────────────
            # frida-tools is the CLI. frida-server.exe runs on the
            # Windows host (we connect to 127.0.0.1:27042 via the WSL
            # NAT — same shared setup as OpenMare/openrecet).  No wine:
            # Lords2.exe runs via WSLInterop on the actual Windows host,
            # which gives us real ddraw + dsound + dplayx + smackw32
            # with zero setup.
            frida-tools

            # ── build toolchain (32-bit Win32 target) ─────────────────
            mingw32.gcc       # i686-w64-mingw32-gcc — produces Win32 PE
            mingw32.binutils
            gnumake
            cmake
            ninja
            pkg-config

            # ── headless testing / image work ────────────────────────
            # No xvfb / scrot — the exe runs via WSLInterop on the host
            # Windows desktop with --hide-window from the Frida agent,
            # and frame capture happens inside the engine via DDraw
            # surface locks (or, for our reimpl, an explicit
            # --capture-to flag).
            ffmpeg            # frame extraction from any captures

            # ── image/asset processing ────────────────────────────────
            imagemagick
            pngquant
            optipng

            # ── python ────────────────────────────────────────────────
            pythonEnv

            # ── docs / reporting ──────────────────────────────────────
            pandoc

            # ── general dev ────────────────────────────────────────────
            git
            git-lfs
            jq
            ripgrep
            fd
            bat
            tree

            # ── JS syntax-checker for the Frida agent ─────────────────
            # The agent (`tools/frida/openlords2-agent.js`) runs inside
            # the Frida QuickJS runtime on the Windows host; we never
            # ship it through node. But node's `--check` parser catches
            # strict-mode violations and basic syntax errors offline so
            # we don't discover them at spawn time as a `frida-error`
            # event.  Usage:
            #   node --check tools/frida/openlords2-agent.js
            nodejs-slim
          ];

          shellHook = ''
            export OPENLORDS2_ROOT=$PWD
            export OPENLORDS2_GAME_DIR="/mnt/c/GOG Games/Lords of the Realm II"

            # Propagate OPENLORDS2_GAME_DIR across the WSL→Windows boundary
            # so .exes launched via WSLInterop can see it. The /p flag
            # tells WSL to auto-translate /mnt/X/... to X:\... when crossing
            # into Windows, so the .exe gets a native Windows path with no
            # client-side translation needed.
            # OL2_* are dev/test toggles read by getenv() inside the .exe
            # (OL2_NO_HINTS suppresses the first-time tutorial scrolls;
            # OL2_FORCE_AI_READY parks the AI realms ready so End-Turn alone
            # advances the turn) — list them so they cross WSL→Windows too.
            export WSLENV="''${WSLENV:+$WSLENV:}OPENLORDS2_GAME_DIR/p:WSL_DISTRO_NAME:OL2_NO_HINTS:OL2_FORCE_AI_READY"

            # mingw cross-compiler convenience aliases.
            export MINGW_CC=i686-w64-mingw32-gcc
            export MINGW_AR=i686-w64-mingw32-ar
            export MINGW_STRIP=i686-w64-mingw32-strip

            # Trace Studio v3 native viewer deps (tools/trace_studio_v3/viewer).
            # Dear ImGui source + header-only nlohmann/json (parses view.json).
            export IMGUI_SRC=${imguiSrc}
            export NLOHMANN_JSON_INC=${pkgs.nlohmann_json}/include

            # Same Frida instance the OpenMare / openrecet siblings use.
            # The Windows-side frida-server.exe binds 0.0.0.0:27042, but WSL2's
            # Hyper-V firewall blocks the 127.0.0.1 loopback path to it — reach
            # it by hostname (cutestation.soy) instead. A 127.0.0.1 default also
            # makes the harness attempt a spurious elevated frida-server launch
            # (UAC) on connect timeout. See memory reference_frida_remote.
            # Override if you run a separate instance for OpenLords2.
            export OPENLORDS2_FRIDA_REMOTE="''${OPENLORDS2_FRIDA_REMOTE:-cutestation.soy:27042}"

            # banner on stderr: `nix develop -c CMD` output stays clean for tools
            {
              echo "openlords2 dev shell — retail runs in rhost: python3 tools/rhost/rhost.py (README there)"
              echo "  mingw cc: $(command -v $MINGW_CC || echo '(missing)')"
              echo "  targets:  ssh win (C:\\ol2-rhost) · wine headless (/mnt/ephemeral/openlords2/rhost-wine)"
              echo "  legacy:   Frida drive (OPENLORDS2_FRIDA_REMOTE=$OPENLORDS2_FRIDA_REMOTE, WSL paths)"
            } >&2
          '';
        };

        # Package output: the openlords2.exe binary cross-compiled with
        # mingw32.  Stub — wired up once src/ has a buildable program.
        packages.openlords2 = pkgs.stdenv.mkDerivation {
          pname = "openlords2";
          version = "0.0.0-dev";
          src = ./src;

          nativeBuildInputs = [ mingw32.gcc mingw32.binutils pkgs.gnumake ];

          buildPhase = ''
            echo "openlords2 build stub — src/ not yet populated"
            mkdir -p $out/bin
            touch $out/bin/openlords2.exe
          '';

          installPhase = "true";

          meta = with pkgs.lib; {
            description = "Open-source drop-in for Lords2.exe (educational/preservation)";
            license = licenses.mit;
            platforms = [ "x86_64-linux" ];
          };
        };
      });
}
