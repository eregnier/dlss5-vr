# VR DLSS 5 : Intégration LukeRoss REAL VR & DLSS 5 Neural Rendering

Ce projet fournit les outils, l'architecture et la documentation permettant de faire fonctionner simultanément le **mod REAL VR de LukeRoss** (OpenXR / SteamVR) et le moteur **DLSS 5 Neural Rendering** (ReShade Add-on build + `renodx-dlss5` + modèle `nvngx_dlssnr.dll`), **sans décompiler ni recompiler de binaires**.

---

## Sommaire

1. [Le Problème & La Solution](#le-problème--la-solution)
2. [Structure du Dépôt](#structure-du-dépôt)
3. [Démarrage Rapide avec `vr-dlss5-patch`](#démarrage-rapide-avec-vr-dlss5-patch)
4. [Commandes & Options](#commandes--options)
5. [Fonctionnement en Jeu](#fonctionnement-en-jeu)
6. [Documentation Détaillée](#documentation-détaillée)

---

## Le Problème & La Solution

### Le conflit initial
- Le mod VR de LukeRoss utilise une version interne de **ReShade 4.9.1** embarquée dans `RealVR64.dll` (déployée en tant que `dxgi.dll`). Cette version ne prend pas en charge l'API ReShade Add-on (introduite en ReShade 5.0+).
- Le DLSS 5 (`renodx-dlss5.addon64`) est un **add-on natif C++** nécessitant **ReShade 6.x avec support Add-on**.
- Par défaut, les deux outils réclament le même nom de fichier proxy (`dxgi.dll`) : l'un écrase l'autre (soit on perd la VR, soit on perd le DLSS 5).

### La solution : Le Chaînage de Proxies (Double Hooking)
Sans toucher aux binaires :
1. **`dxgi.dll`** reste le mod **LukeRoss VR** : il initialise le casque VR (OpenXR / SteamVR), gère la caméra stéréoscopique et applique le correctif de jitter DLSS par œil.
2. **`dinput8.dll`** (ou `d3d12.dll`) reçoit **ReShade 6.8+ (Add-on build)** : chargé automatiquement par le jeu, il détecte et charge `renodx-dlss5.addon64` et le modèle neuronal `nvngx_dlssnr.dll`.

```
[ Jeu (ex: afop.exe) ]
     │
     ├──> Charge dxgi.dll    ──> Mod LukeRoss REAL VR (OpenXR, Stéréoscopie, Fix DLSS)
     │
     └──> Charge dinput8.dll ──> ReShade 6.8 Add-on Build
                                       │
                                       └──> renodx-dlss5.addon64
                                       └──> nvngx_dlssnr.dll (DLSS 5 Neural Engine)
```

---

## Structure du Dépôt

```
vrdlss5/
├── README.md                      # Ce fichier
├── GUIDE_INTEGRATION_VR_DLSS5.md  # Étude technique complète & analyse architecturale
│
├── vr-dlss5-patch/                # Outil autonome écrit en Go (CLI d'automatisation)
│   ├── main.go                    # Point d'entrée CLI
│   ├── detector/                  # Analyse PE (64/32 bits, DLLs importées, détection LukeRoss/DLSS)
│   ├── downloader/                # Téléchargement & cache (ReShade Addon, renodx, dlssnr)
│   ├── installer/                 # Déploiement intelligent & configuration ReShade.ini
│   ├── vr-dlss5-patch.exe         # Binaire compilé prêt à l'emploi
│   └── README.md                  # Documentation spécifique à l'outil Go
│
├── DLSS5oneclick/                 # Projet Rust amont DLSS 5 One-Click
├── RealRepo/                      # Répertoire des profils de jeu LukeRoss REAL VR
└── RealConfig.bat                 # Script de configuration officiel LukeRoss
```

---

## Démarrage Rapide avec `vr-dlss5-patch`

L'outil Go `vr-dlss5-patch.exe` automatise l'ensemble du processus : détection, téléchargement des fichiers requis, choix du proxy approprié et configuration.

### 1. Exemple : Patcher un jeu (ex: Avatar Frontiers of Pandora)

```powershell
cd C:\code\vrdlss5\vr-dlss5-patch
.\vr-dlss5-patch.exe "D:\Games\AFOP\afop.exe"
```

L'outil va :
1. Analyser l'exécutable `afop.exe` (architecture 64-bit, imports `DINPUT8.dll`, DX12, etc.).
2. Détecter la présence du mod VR LukeRoss dans `dxgi.dll`.
3. Conserver `dxgi.dll` intact pour préserver la VR.
4. Télécharger et extraire ReShade 6.8 Addon, `renodx-dlss5.addon64` et `nvngx_dlssnr.dll`.
5. Déployer ReShade en tant que **`dinput8.dll`**.
6. Configurer `ReShade.ini` (`[RenoDX.DLSS5] NeuralUplift=1`).

---

## Commandes & Options

| Commande | Description |
| :--- | :--- |
| `vr-dlss5-patch.exe <chemin>` | Analyse et installe le patch sur l'exécutable ou le dossier cible. |
| `vr-dlss5-patch.exe -check <chemin>` | Diagnostic complet sans écrire ni modifier de fichier. |
| `vr-dlss5-patch.exe -dry-run <chemin>` | Simule les étapes d'installation en affichant les actions prévues. |
| `vr-dlss5-patch.exe -remove <chemin>` | Désinstalle proprement tous les fichiers déposés par le patch. |
| `vr-dlss5-patch.exe -proxy <nom> <chemin>` | Force un nom de DLL proxy spécifique (ex: `dinput8.dll`, `d3d12.dll`). |
| `vr-dlss5-patch.exe -cache-dir <dossier>` | Définit un répertoire de cache personnalisé pour les téléchargements. |

### Compilation depuis les sources

```powershell
cd vr-dlss5-patch
go build -o vr-dlss5-patch.exe main.go
```

---

## Fonctionnement en Jeu

1. Allumez votre casque VR (Meta Quest, Pimax, Valve Index, etc.) et lancez SteamVR ou le runtime OpenXR.
2. Démarrez le jeu normalement via son lanceur ou son exécutable.
3. Le jeu bascule dans le casque VR en 3D stéréoscopique.
4. **Touches de contrôle** :
   - **`[Home]`** : Ouvre l'interface ReShade. Dans l'onglet **Add-ons**, vérifiez la présence du panneau *DLSS 5 Neural Rendering*.
   - **`[F6]`** : Raccourci direct pour activer / désactiver le Neural Rendering.
   - **`[Pavé Numérique]`** : Menu de réglages caméra et confort du mod LukeRoss VR.

---

## Documentation Détaillée

Pour comprendre le fonctionnement bas niveau des hooks DirectX, de l'API NGX et de l'ordonnancement des appels stéréoscopiques, consultez :
- [GUIDE_INTEGRATION_VR_DLSS5.md](GUIDE_INTEGRATION_VR_DLSS5.md) : Rapport d'analyse technique et guide de conception.
- [vr-dlss5-patch/README.md](vr-dlss5-patch/README.md) : Documentation détaillée du sous-projet Go.
