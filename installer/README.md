# DLSS 5 <> VR — Universal Installer (`VR-DLSS5-Installer.exe`)

Installateur graphique universel ultra-léger (~150 Ko) écrit en C++ natif Win32 pour déployer **DLSS 5 Neural Reconstruction** sur n'importe quel jeu équipé du **mod LukeRoss R.E.A.L. VR**.

---

## Fonctionnalités

1. **Interface Graphique Pure Win32 (Zéro dépendance)** :
   - Fonctionne nativement sur Windows 10 et 11 (High-DPI aware, polices vectorielles Segoe UI).
   - Pas de .NET, pas d'Electron, pas de Python, pas de runtime lourd.
2. **Sélection simple & Glisser-Déposer (Drag & Drop)** :
   - Parcourez votre disque avec le bouton `Browse...` ou glissez-déposez directement l'exécutable (`.exe`) du jeu ou son dossier dans la fenêtre.
3. **Détection Intelligente & Anti-Erreurs** :
   - **Détection LukeRoss** : Vérifie la présence de `RealVR64.dll`, `RealVR.ini` ou du `dxgi.dll` d'origine de LukeRoss.
   - **Détection Unreal Engine** : Si vous sélectionnez l'exécutable racine d'un jeu Unreal Engine, l'installateur résout automatiquement le dossier réel `Binaries\Win64`.
   - **Détection Moteur DLSS** : Vérifie la présence de `nvngx_dlss.dll` ou NVIDIA Streamline (`sl.dlss.dll`).
   - **Détection Processus Verrouillé** : Avertit si le jeu est en cours d'exécution et propose de le fermer proprement avant de copier.
4. **Mécanisme de Swap Sécurisé (Idempotent)** :
   - Ne détruit jamais le mod VR de LukeRoss : renomme automatiquement `dxgi.dll` (LukeRoss) $\rightarrow$ `RealVR64.dll`.
   - Si relancé ultérieurement pour mettre à jour, il détecte que `RealVR64.dll` existe déjà et met à jour uniquement le proxy sans écraser LukeRoss.
5. **Restauration en 1 Clic (Rollback)** :
   - Le bouton `Restore LukeRoss Vanilla` retire le proxy DLSS 5 et rétablit instantanément le mod LukeRoss d'origine (`RealVR64.dll` $\rightarrow$ `dxgi.dll`).

---

## Compilation

Le script `build.bat` compile le projet avec Microsoft Visual C++ :

```cmd
cd installer
build.bat
```

Génère `VR-DLSS5-Installer.exe` (150 Ko).

---

## Utilisation

1. Double-cliquez sur `VR-DLSS5-Installer.exe`.
2. Sélectionnez l'exécutable de votre jeu (ex: `Cyberpunk2077.exe`, `afop.exe`, etc.).
3. Vérifiez le panneau de diagnostic.
4. Cliquez sur **`Install / Update DLSS 5`**.
5. Lancez votre jeu en VR !
