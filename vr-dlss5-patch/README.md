# vr-dlss5-patch

Outil autonome en Go permettant d'intégrer le **DLSS 5 (Neural Rendering)** dans un jeu tournant sous le mod **REAL VR de LukeRoss** (ou en standalone).

## Principe de fonctionnement

1. **Détection du jeu & du binaire PE** :
   - Inspecte l'exécutable (`.exe`), son architecture (64-bit/32-bit), ses DLLs importées (`DINPUT8.dll`, `d3d12.dll`, `dxgi.dll`...).
   - Vérifie la présence d'un DLSS natif (`nvngx_dlss.dll`, `sl.dlss.dll`).
2. **Détection du mod VR LukeRoss** :
   - Vérifie si `dxgi.dll` est occupé par le mod VR (`RealVR64.dll`).
   - Si LukeRoss est présent, l'outil **ne touche jamais à `dxgi.dll`** pour préserver l'initialisation OpenXR / SteamVR et le rendu stéréoscopique.
   - ReShade (version Add-on 6.8+) est automatiquement déployé sous un point d'injection secondaire supporté par l'exécutable : **`dinput8.dll`** (ou `d3d12.dll`).
3. **Récupération des composants DLSS 5** :
   - Télécharge automatiquement depuis reshade.me et GitHub `RankFTW/rhi-repo` (sans nécessiter de token API) :
     - `ReShade64.dll` (avec support Add-on)
     - `renodx-dlss5.addon64` (add-on DLSS 5)
     - `nvngx_dlssnr.dll` (modèle neuronal ShortFuse multi-générations RTX)
     - `dlss5-bridge.addon64` (si jeu DX11 avec DLSS natif)
   - Met en cache les fichiers dans `%LOCALAPPDATA%\vr-dlss5-patch\cache`.
4. **Configuration automatique de `ReShade.ini`** :
   - Active `[RenoDX.DLSS5] NeuralUplift=1`.
   - S'assure qu'aucun add-on n'est désactivé dans `[ADDON]`.

---

## Compilation

```powershell
cd vr-dlss5-patch
go build -o vr-dlss5-patch.exe main.go
```

---

## Utilisation

### Appliquer le patch sur un jeu
```powershell
.\vr-dlss5-patch.exe "D:\Games\AFOP\afop.exe"
# ou en passant le dossier du jeu
.\vr-dlss5-patch.exe "D:\Games\AFOP"
```

### Vérifier / Diagnostiquer sans modifier de fichier
```powershell
.\vr-dlss5-patch.exe -check "D:\Games\AFOP"
```

### Simuler les actions (dry-run)
```powershell
.\vr-dlss5-patch.exe -dry-run "D:\Games\AFOP"
```

### Désinstaller le patch
```powershell
.\vr-dlss5-patch.exe -remove "D:\Games\AFOP"
```

### Forcer le nom de la DLL proxy ReShade
```powershell
.\vr-dlss5-patch.exe -proxy dinput8.dll "D:\Games\AFOP"
```

---

## En jeu

- **Home** : Ouvre l'interface ReShade -> Onglet *Add-ons* pour surveiller l'état du *DLSS 5 Neural Rendering*.
- **F6** : Raccourci d'activation / désactivation directe du Neural Rendering.
- **Pavé numérique** : Menu de réglages du mod LukeRoss VR.
