# DLSS 5 <> VR Dual-Proxy (`dxgi.dll`)

Ce dossier contient le code source C++ et les fichiers de compilation du proxy hybride `dxgi.dll` pour mod VR LukeRoss + DLSS 5 Neural Reconstruction.

## Architecture

Le DLL `dxgi.dll` agit comme un double proxy :
1. **Routage Système & Mod VR** : Détecte `RealVR64.dll` (LukeRoss) et intercepte les exports DXGI système (`dxgi_orig.dll` ou `C:\Windows\System32\dxgi.dll`).
2. **Débridage Neural Evaluation Loop** : Patche en mémoire vive les offsets d'évaluation continue (`0x3CC0`, `0x40E0`, `0xDFF5`) pour forcer RenoDX à évaluer DLSS 5 à chaque frame en VR stéréo.
3. **VR & Desktop In-Game HUD ("DLSS 5 <> VR")** :
   - Rendu haute définition Segoe UI via GDI rasterizer anti-aliasé.
   - En VR : Overlay OpenVR flottant ancré dans le champ de vision (World/Head), toggleable via manette (`Select / Back + L3 / Stick Click`).
   - Sur Bureau : Overlay transparent `layered window` Win32, toggleable via `Insert` / `F6`.

## Fichiers

- `proxy.cpp` : Code source C++ complet du dual-proxy, hooks et HUD.
- `proxy.def` : Définition des exports DXGI (23 fonctions exportées).
- `openvr.h` & `openvr_api.dll` : API OpenVR pour la création et gestion de l'overlay VR natif.
- `build.bat` : Script de compilation autonome en un clic (détecte Visual Studio BuildTools / Community).
- `deploy.ps1` : Script PowerShell de déploiement automatique vers le dossier du jeu.

## Compilation & Déploiement

Pour compiler :
```cmd
build.bat
```

Pour déployer sur le jeu (ex: *Avatar: Frontiers of Pandora*) :
```powershell
.\deploy.ps1 -GameDir "D:\Games\AFOP"
```
