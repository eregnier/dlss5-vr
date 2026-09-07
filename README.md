# DLSS 5 <> VR : Intégration LukeRoss REAL VR & DLSS 5 Neural Reconstruction

Ce dépôt fournit l'architecture complète, le double proxy C++ haute performance, les outils d'automatisation et la documentation technique permettant de faire fonctionner simultanément le **mod REAL VR de LukeRoss** (OpenXR / SteamVR) et le moteur **DLSS 5 Neural Reconstruction** (ReShade 6.8+ Add-on + `renodx-dlss5` + `nvngx_dlssnr.dll`), avec un HUD de contrôle in-game natif (VR & Bureau) et le débridage de l'évaluation continue en temps réel.

---

## Sommaire

1. [Architecture & Fonctionnalités](#architecture--fonctionnalités)
2. [Organisation du Répertoire](#organisation-du-répertoire)
3. [Compilation & Déploiement Rapide](#compilation--déploiement-rapide)
4. [Contrôles In-Game (HUD & Raccourcis)](#contrôles-in-game-hud--raccourcis)
5. [Outils & Diagnostics](#outils--diagnostics)
6. [Documentation Complète](#documentation-complète)

---

## Architecture & Fonctionnalités

### Le Problème
- Le mod VR de LukeRoss intercepte `dxgi.dll` pour injecter son moteur stéréoscopique et son runtime OpenXR/SteamVR.
- Le moteur DLSS 5 Neural Rendering (ReShade Add-on) requiert ReShade 6.8+, l'accès direct aux buffers D3D12, et évalue habituellement le rendu à chaque frame desktop.
- En VR, RenoDX se désactivait après la première frame (mise en sommeil de la boucle d'évaluation), et les deux frameworks entraient en conflit sur le hook DXGI.

### La Solution Hybride (Dual-Proxy C++ & Memory Patching)
1. **Double Proxy `proxy/dxgi.dll`** : Intercepte les 23 exports système DXGI et s'intercale en toute transparence entre le jeu, LukeRoss (`RealVR64.dll`) et ReShade.
2. **Débridage Mémoire Dynamique (Runtime Memory Uncap)** : Patche en RAM les verrous de la boucle d'évaluation RenoDX (`0x3CC0`, `0x40E0`, `0xDFF5`) pour garantir une reconstruction neuronale continue à 100% du framerate VR.
3. **HUD In-Game Natif ("DLSS 5 <> VR")** :
   - Rasterizer vectoriel haute netteté (GDI Segoe UI anti-aliasé, 480×220).
   - **En VR** : Overlay OpenVR 3D immersif (positionnable en Top, Bottom, World ou Head).
   - **Sur Bureau** : Fenêtre transparente ultra-légère (`layered window`).
   - Pilote directement RenoDX et ReShade en mémoire vive sans bloquer les entrées du jeu.

---

## Organisation du Répertoire

```
vrdlss5/
├── proxy/                         # C++ Dual-Proxy (Composant cœur actif)
│   ├── proxy.cpp                  # Code source C++ (Hooks DXGI, HUD Segoe UI, OpenVR, Patches RAM)
│   ├── proxy.def                  # Définition des 23 exports DXGI
│   ├── openvr.h / openvr_api.dll  # SDK & runtime OpenVR pour l'overlay VR natif
│   ├── build.bat                  # Script de compilation MSVC en 1 clic
│   ├── deploy.ps1                 # Script de déploiement vers le jeu
│   └── README.md                  # Documentation technique du proxy
│
├── vr-dlss5-patch/                # CLI autonome en Go (Installateur automatisé)
│   ├── main.go                    # Point d'entrée CLI
│   ├── detector/                  # Analyseur PE & détection automatique
│   ├── downloader/                # Téléchargement automatique des composants DLSS 5 & ReShade
│   ├── installer/                 # Déploiement et configuration des profils
│   └── README.md                  # Documentation de l'outil Go
│
├── deps/                          # Dépendances binaires externes précompilées
│   ├── renodx-dlss5.addon64       # Add-on RenoDX DLSS 5 Neural Reconstruction
│   ├── cudart64_12.dll            # Runtime NVIDIA CUDA 12 pour nvngx_dlssnr.dll
│   └── README.md                  # Description des dépendances
│
├── tools/                         # Boîte à outils Python & Scripts de diagnostic
│   ├── diag.py                    # Diagnostic complet en temps réel (processus, hooks, RAM)
│   ├── tail_log.py                # Moniteur de logs ReShade et LukeRoss en direct
│   ├── patch_workset.py           # Analyseur de l'allocation mémoire RenoDX
│   ├── analyze_eval.py            # Analyseur de boucle d'évaluation
│   ├── audit/                     # Rapports d'audits, désassemblages et tables PE
│   └── README.md                  # Guide d'utilisation des outils
│
├── RealRepo/                      # Répertoire officiel des profils LukeRoss REAL VR
├── RealConfig.bat                 # Script de configuration officiel LukeRoss
├── GUIDE_INTEGRATION_VR_DLSS5.md  # Guide pas-à-pas d'intégration
├── REX_INTEGRATION_VR_DLSS5.md    # Rapport d'architecture détaillé et retour d'expérience
└── README.md                      # Ce document
```

---

## Compilation & Déploiement Rapide

### 1. Compiler le Proxy C++

Le script `proxy/build.bat` détecte automatiquement Visual Studio (BuildTools, Community, Professional ou Enterprise) et génère `dxgi.dll` sans polluer le dossier :

```cmd
cd proxy
build.bat
```

### 2. Déployer sur votre jeu

Déployez en une commande vers le dossier de votre jeu (ex: *Avatar: Frontiers of Pandora*) :

```powershell
cd proxy
.\deploy.ps1 -GameDir "D:\Games\AFOP"
```

Pour forcer la fermeture du jeu s'il est déjà en cours d'exécution :
```powershell
.\deploy.ps1 -GameDir "D:\Games\AFOP" -ForceClose
```

---

## Contrôles In-Game (HUD & Raccourcis)

Le menu **DLSS 5 <> VR** apparaît directement dans le casque VR et sur l'écran bureau :

| Action | Raccourci Manette VR | Raccourci Clavier |
| :--- | :--- | :--- |
| **Afficher / Masquer le HUD** | `Select / Back` + `L3` (Stick Click) | `Insert` |
| **Basculer le DLSS 5 On / Off** | `Select` (dans le HUD) | `F6` |
| **Navigation dans le menu** | `Croix directionnelle (D-Pad)` Haut / Bas | Flèches Haut / Bas |
| **Changer la Position du HUD** | Navigation sur la ligne `HUD Pos` (`Top` / `Bottom` / `World` / `Head`) | Flèches Gauche / Droite |
| **Ouvrir le menu ReShade** | — | `Home` |
| **Menu Réglages LukeRoss VR** | Bouton dédié VR | `Pavé Numérique` |

---

## Outils & Diagnostics

Pour vérifier le bon fonctionnement du proxy et des patches mémoire lorsque le jeu tourne :

```powershell
# Diagnostic complet des hooks et de l'état mémoire
python tools/diag.py

# Suivi en temps réel des logs ReShade
python tools/tail_log.py
```

---

## Documentation Complète

Pour approfondir les mécanismes internes :
- [GUIDE_INTEGRATION_VR_DLSS5.md](GUIDE_INTEGRATION_VR_DLSS5.md) : Guide de mise en œuvre étape par étape.
- [REX_INTEGRATION_VR_DLSS5.md](REX_INTEGRATION_VR_DLSS5.md) : Rapport d'architecture, analyse des hooks DXGI, du modèle de threading et résolution des conflits.
- [proxy/README.md](proxy/README.md) : Documentation technique du double proxy C++.
- [tools/README.md](tools/README.md) : Documentation des outils Python et des fichiers d'audit.
