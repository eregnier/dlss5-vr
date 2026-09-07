# DLSS 5 <> VR Dual-Proxy (`dxgi.dll`)

Ce dossier contient le code source C++ et les fichiers de compilation du proxy hybride `dxgi.dll` pour mod VR LukeRoss + DLSS 5 Neural Reconstruction.

## Architecture

Le DLL `dxgi.dll` agit comme un double proxy :
1. **Routage Système & Mod VR** : Détecte `RealVR64.dll` (LukeRoss) et intercepte les exports DXGI système (`C:\Windows\System32\dxgi.dll`).
2. **Débridage Neural Evaluation Loop** : Patche en mémoire vive les offsets d'évaluation continue (`0x3CC0`, `0x40E0`, `0xDFF5`) pour forcer RenoDX à évaluer DLSS 5 à chaque frame en VR stéréo.
3. **VR & Desktop In-Game HUD ("DLSS 5 <> VR")** :
   - Rendu haute définition Segoe UI via GDI rasterizer anti-aliasé.
   - En VR : Overlay OpenVR flottant ancré dans le champ de vision (4 ancres au choix, 3 échelles), toggleable via manette (`Select / Back + L3 / Stick Click`) ou clavier (`F6`).
   - Sur Bureau : Overlay transparent `layered window` Win32, toggleable via `F6`.
4. **Isolation D-Pad (MinHook)** :
   - Interception de `XInputGetState`, `XInputGetStateEx` (ordinal 100) et `joyGetPosEx` sur `RealVR64.dll`, `XINPUT1_4.dll`, `XINPUT1_3.dll`, `XINPUT9_1_0.dll` et `winmm.dll`.
   - Masquage automatique des touches directionnelles (D-Pad) vers le moteur de jeu quand le menu est ouvert. Permet de se déplacer (stick gauche), viser (stick droit), sauter et tirer sans déclencher de potions/consommables in-game.
5. **Auto-récupération OpenVR (Self-Healing)** :
   - Purge continue de la file d'événements IPC SteamVR via `PollNextOverlayEvent()` pour éliminer la saturation (erreur 23 `VROverlayError_RequestFailed`).
   - Destruction et recréation transparente de l'overlay handle en cas de mise en veille du casque ou réinitialisation SteamVR.

## Fichiers

- `proxy.cpp` : Code source C++ complet du dual-proxy, hooks MinHook, OpenVR overlay et HUD.
- `proxy.def` : Définition des 24 exports (20 DXGI + 3 NGX + `XInputGetState`).
- `minhook/` : Moteur de hook API inline ultra-léger embarqué.
- `openvr.h` & `openvr_api.dll` : API OpenVR pour la création et gestion de l'overlay VR natif.
- `build.bat` : Script de compilation autonome en un clic (MSVC x64).
- `deploy.ps1` : Script PowerShell de déploiement automatique vers le dossier du jeu.

## Compilation & Déploiement

Pour compiler :
```cmd
build.bat
```

Pour déployer sur le jeu (ex: *Star Wars Outlaws*) :
```powershell
.\deploy.ps1 -GameDir "C:\Program Files (x86)\Steam\steamapps\common\Star Wars Outlaws"
```
