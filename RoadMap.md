Phase 0 – Projektfundament (1–2 Meilensteine)

Ziel: Repo-Struktur, Build, “Hello Window”, Logging, Testbarkeit.

Repo-Struktur (Beispiel)

native/ (Host + Vulkan + Physik-Bridges)

python/ (gameplay, editor, scripts)

assets/ (textures, sounds, maps)

shaders/ (SPIR-V output)

tools/ (asset packer, map compiler)

Build-System (CMake oder Cargo + CMake für Shader)

CI lokal: Debug/Release Builds

Minimal-Logging + Crashdump/Assert-Policy

Native Host öffnet Fenster, pumpt Events, hat Renderloop (noch ohne Vulkan ok)

Exit-Kriterium: Game.exe startet zuverlässig, zeigt Fenster, loggt sauber.

Phase 1 – Vulkan Core “nicht hübsch, aber echt”

Ziel: Vulkan läuft stabil: Swapchain, Command Buffers, Synchronisation.

Vulkan Instance/Device/Queues

Swapchain + Resize Handling

Framegraph minimal: Acquire → Record → Submit → Present

Clear Screen

Debug Layer + RenderDoc-freundlich

Shader-Pipeline: HLSL/GLSL → SPIR-V (offline build step)

Exit-Kriterium: Stabiler Clear + Resize + keine Validation-Errors.

Phase 2 – Renderer MVP: Dreieck → Textur → Sprite

Ziel: Du kannst etwas zeichnen, mit Assets.

Triangle

Textured Quad (Sampler, descriptor sets)

Sprite Renderer:

Billboard (immer zur Kamera)

SpriteSheet Animation (Frame Index + Timing)

Asset Loader (PNG/KTX2 später, aber erstmal PNG ok)

Material-System minimal (Texture + params)

Exit-Kriterium: Animiertes Sprite im Raum + stabile FPS.

Phase 3 – Core-Loop, Input, Entity System (minimal aber sauber)

Ziel: Bewegliches Spielgefühl + Interaktion ohne BSP.

Fixed timestep Simulation + render interpolation

Input (Keyboard/Mouse)

Entity/Component Basissystem:

Transform

Renderable (Sprite/Mesh)

Collider (noch simple)

Script/Behavior (Python)

Python Runtime embedded:

Host lädt Python

ruft game.update(dt) und game.on_event(...) auf

Python kann Entities erstellen/ändern

Exit-Kriterium: Player kann laufen, gucken, Entities spawnen per Python.

Phase 4 – BSP Welt: Laden, Rendern, Kollision

Ziel: Jetzt kommt dein Herzstück.

Map-Format festlegen (z. B. eigenes JSON/GLTF+Metadata/Quake-like)

BSP Builder/Compiler (Tool)

Static geometry → BSP tree

Surfaces + Materials

Portale/Sektoren optional (wenn du Portal-Clipping willst)

Runtime:

BSP Traversal für Sichtbarkeit (front-to-back)

Static collision gegen BSP (player)

Static world rendering (Walls/Floors/Ceilings)

Exit-Kriterium: Du läufst in einer BSP-Map rum, Wände occluden korrekt.

Phase 5 – Interaktion: Türen, Buttons, Trigger, Events

Ziel: “Spielbarkeit” entsteht.

Raycast/Use-System:

Mittelpunkt-Screen Ray → Interactable

UsePressed Event

Trigger volumes:

OnEnter, OnExit

Türen/Plattformen als Entities (NICHT BSP ändern!)

Animator: position/rotation curves

Collider bewegt sich mit

Scripting in Python:

“Wenn Button gedrückt → Tür öffnet”

Sequenzen: Sounds/Lights/Spawns

Exit-Kriterium: Button öffnet Tür, Trigger startet Event, alles per Python definierbar.

Phase 6 – Beleuchtung: “extrem schön” in iterativen Schritten

Ziel: Atmosphäre. Ohne das wird’s nie “dein Spiel”.

Empfohlene Reihenfolge:

Fog + Color grading + Bloom (macht sofort Stimmung)

Forward+ oder Deferred (viele dynamische Lichter)

Shadowing (erst 1–2 “Hero Lights”, später mehr)

SSAO / SSR / volumetric-ish tricks (sparsam!)

Optional später:

Lightmaps für statische Welt + dynamische Lichter additiv

Exit-Kriterium: Du kannst Lichter setzen, Szene wirkt “wow” statt “Testlevel”.

Phase 7 – Partikel, Decals, “Blut & Dreck”

Ziel: Effekte, die “physikalisch wirken”, ohne echte Flüssigkeit zu simulieren.

GPU Partikel (instanced quads)

Kollision “cheap” (depth buffer / simple planes / BSP queries)

Blut:

Spritzpartikel (GPU)

Pfützen = Decals + “spread” shader trick

Funken/Rauch/Nebel

Decal-System (projected or mesh decals)

Exit-Kriterium: Treffer spawnt Blut + Pfütze bleibt, Performance bleibt gut.

Phase 8 – Physik & Ragdolls (richtig, nicht fake)

Ziel: echte Interaktion, ragdoll deaths, props.

Bullet/Physik-Integration nativ

RigidBodies für Props

Character Controller (capsule)

Ragdoll:

Skeleton joints → constraints

Blend: animated → ragdoll

Physics Queries für Gameplay (raycast, sweep)

Exit-Kriterium: Gegner kippt als Ragdoll um, Props reagieren, nichts explodiert numerisch.

Phase 9 – AI, Animation, Gameplay-Schleife

Ziel: “Spiel” statt Techdemo.

AI: state machine (patrol, chase, attack, search)

Animation:

Spritesheet states (oder später Skeletal)

Combat, damage, inventory, weapons

Save/Load minimal

Exit-Kriterium: Level spielbar, Gegner verhalten sich plausibel, Loop macht Spaß.

Phase 10 – UI/Menus/Editor/Tooling

Ziel: Content-Pipeline, nicht nur Code.

UI-System (ImGui für Tools, eigenes UI für Ingame)

Menüs:

Settings, Keybinds, Save/Load

Map-/Trigger-Editor (minimal)

Asset-Packer:

assets.pak bauen

Versionierung/Hashing

Exit-Kriterium: Du kannst Inhalte bauen, ohne im Code rumzuwühlen.

Phase 11 – Packaging: EXE Release (der Endboss)

Ziel: “Doppelklick → Spiel läuft”.

Release Build (native)

Embedded Python Runtime (mitliefern)

Assets/Shaders als Pak oder Ordner

Installer optional (später)

Crash logs + minimal telemetry (lokal)

Exit-Kriterium: Clean folder release: Game.exe + assets.pak + shaders/ → läuft auf anderem PC.

Zwei Regeln, die das Projekt retten

Jede Phase hat ein Exit-Kriterium, sonst sammelst du tote Features.

Renderer/Physik bleiben nativ, Python bleibt Gameplay/Tools.