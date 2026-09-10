# 06. Feuille de Route & Suivi des Avancements (dlss5-perf-docs)

Ce document centralise le plan d'action opérationnel, le suivi des modifications de code, les tests de validation et la feuille de route pour le projet `dlss5-vr`.

---

## 1. Plan d'Action Opérationnel

```mermaid
flowchart LR
    E1["Étape 1 : Package & Binaires"] --> E2["Étape 2 : Script Déploiement Direct"]
    E2 --> E3["Étape 3 : Refonte Installer C++"]
    E3 --> E4["Étape 4 : Adaptation Proxy & VR HUD"]
    E4 --> E5["Étape 5 : Benchmarks & Validation In-Game"]
```

### Étape 1 : Rapatriement & Intégration des Binaires OptiScaler Pre-SR (Complété)
- [x] Clone du dépôt de référence `Kizzuwatnaa/DLSS5-Autopilot`.
- [x] Clone du dépôt `wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass`.
- [x] Téléchargement et extraction de la release officielle validée `OptiScaler-DLSSNR-v0.7.6.zip` :
  - `OptiScaler.dll` (v0.7.6 avec support `RunBeforeSR` et `ResidualAcrossRR`)
  - `nvngx.dll_dlssnr.dll` (Forwarder officiel de signature)
  - Dossier `OptiScaler/` (backends D3D12, FSR 3.1 FG, XeSS FG, kernels nvfp4)
  - `OptiScaler.ini` complet

### Étape 2 : Outillage de Diagnostic & Simulation Mathématique (Complété, outillage retiré en v1.1.0)
> **[Archive]** Les scripts `tools/` et l'ancien outil Go `vr-dlss5-patch` ont été retirés du dépôt en v1.1.0 (le pipeline passe par `VR-DLSS5-Installer.exe`). Les résultats ci-dessous restent valides.
- [x] `tools/vr_perf_simulator.py` : Simulateur mathématique complet validant que `RunBeforeSR` + `WorkingScale=0.75` ramène le temps de trame à 12,89 ms (< 13,88 ms) à 72 Hz.
- [x] `tools/dlss5_binary_inspector.py` : Inspecteur d'en-têtes PE, architectures CUDA (`sm_89`, `sm_90`) et signatures. Découverte de `"OptiScaler.asi loaded and patched"` dans `RealVR64.dll`.
- [x] `tools/optiscaler_vr_configurator.py` : Générateur automatique de configuration optimisée pour Cyberpunk 2077 VR.
- [x] `tools/OptiScaler-VR-Cyberpunk2077.ini` : Profil INI pré-configuré prêt à l'emploi.

### Étape 3 : Script de Déploiement Rapide "One-Click VR" (Complété)
- [x] `tools/deploy_optiscaler_vr.bat` / `.ps1` : Permet d'installer immédiatement le nouveau pipeline OptiScaler Pre-SR sur Cyberpunk 2077 sans passer par ReShade.
- [x] Déploiement automatisé du sous-dossier `OptiScaler/`, de `OptiScaler.asi`, `nvngx.dll_dlssnr.dll` et `OptiScaler.ini` pré-configuré.

### Étape 4 : Refonte de l'Installeur Universel (`installer/installer.cpp`) (Complété)
- [x] Remplacement intégral de la copie de ReShade 6.8 et `renodx-dlss5.addon64` par le déploiement d'OptiScaler Pre-SR (`OptiScaler.asi`, `OptiScaler.dll`, `nvngx.dll_dlssnr.dll`).
- [x] Déploiement récursif automatique du répertoire `OptiScaler/` contenant les backends DirectX 12 et shaders nécessaires.
- [x] Nettoyage proactif des anciens fichiers ReShade / RenoDX pour éviter tout conflit ou crash mémoire.
- [x] Configuration native d'`OptiScaler.ini` (`RunBeforeSR=true`, `WorkingScale=0.75`, `Passes=1`, `ResidualAcrossRR=true`, `Preset=2`).
- [x] Fonction de restauration propre (`DoRestore`) nettoyant les dépendances OptiScaler et restaurant `RealVR64.dll` -> `dxgi.dll`.
- [x] Compilation sans erreur ni avertissement (`VR-DLSS5-Installer.exe`).

### Étape 5 : Modernisation du Dual-Proxy & VR HUD (`proxy/proxy.cpp`) (Complété)
- [x] Suppression de tous les anciens patches d'urgence et offsets mémoire RenoDX.
- [x] Chargement explicite d'OptiScaler Pre-SR (`OptiScaler.asi` / `OptiScaler.dll`) assurant la coopération stéréoscopique native avec LukeRoss (`RealVR64.dll`).
- [x] VR HUD OpenVR réécrit pour OptiScaler Pre-SR :
  - **Ligne 0** : `Neural Engine` (`[ ACTIVE ]` / `[ BYPASS ]`)
  - **Ligne 1** : `VR WorkingScale` (`0.50x [Ultra-Fast]`, `0.66x [Balanced]`, `0.75x [72fps Solid]`, `1.00x [Native 4K]`) avec jauge visuelle
  - **Ligne 2** : `Placement Mode` (`Pre-SR [Render Res - Fast]` / `Post-SR [Output 4K - Heavy]`)
  - **Ligne 3** : `AI Model Preset` (`Preset 0 [DLSS-D RR]`, `Preset 1 [Ultra]`, `Preset 2 [Performance - VR]`)
  - **Ligne 4** : `Ray Recon (RR)` (`ResidualAcrossRR [ON - Preserved]` / `[OFF]`)
  - **Ligne 5** : `VR HUD Display` (`Pos: Bottom-Center | Scale: 1.5x`)
- [x] Implémentation du **Dynamic VR Frame Guard** : analyse en continu des temps de trame par œil et abaissement automatique du `WorkingScale` si la trame flirte avec le seuil V-Sync (13,0 ms à 72 Hz) afin d'éviter la chute sous reprojection.
- [x] Sauvegarde différée 500 ms (debounce) vers `OptiScaler.ini`.
- [x] Compilation sans erreur (`dxgi.dll`) et packaging autonome dans `dist/DLSS5-VR-Release.zip`.

---

## 2. Journal des Découvertes & Décisions Techniques

| Date | Découverte / Décision | Rationale & Impact |
| :--- | :--- | :--- |
| **10/09/2026** | **Découverte du Seuil V-Sync 13,88 ms** | L'analyse clinique a démontré que la chute à 40 FPS était causée par le dépassement de la fenêtre 72 Hz (21,7 ms requis par RenoDX Post-SR). |
| **10/09/2026** | **Analyse de wilsjo2 Pre-SR Multipass** | `RunBeforeSR=true` divise la surface de calcul par 4 en s'exécutant sur l'entrée de rendu 1080p au lieu de la sortie 4K par œil. |
| **10/09/2026** | **Rétro-ingénierie de `RealVR64.dll`** | Découverte de la chaîne `OptiScaler.asi loaded and patched` : LukeRoss a nativement prévu de coopérer avec OptiScaler ! |
| **10/09/2026** | **Abandon officiel de ReShade 6.8 Add-on** | ReShade n'apporte aucune plus-value en VR face à OptiScaler et crée un overhead CPU/GPU non maîtrisable. |
| **10/09/2026** | **Prise en charge de `ResidualAcrossRR`** | Permet d'activer Ray Reconstruction dans Cyberpunk 2077 sans scintillements ni perte de performance. |
| **10/09/2026** | **Canal de contrôle live OptiScaler** | OptiScaler ne relit son INI qu'au démarrage : ajout d'un bloc partagé `Local\VRDLSS5_Control_1_<pid>` (fork wilsjo2 patché, `deps/OptiScaler.dll`) pour appliquer les réglages du HUD à chaud. |
| **10/09/2026** | **Moteur concurrent `WINMM.dll` identifié** | Un ancien build `V23040-preSR-PR6` chargé depuis le dossier du jeu exécutait la passe neuronale à la place de notre `OptiScaler.asi` v0.7.6 : mis en quarantaine (`WINMM.dll.disabled`). |
| **10/09/2026** | **D-Pad lu via le parseur HID** | Cyberpunk (pad type DualSense) lit le D-Pad via `HidP_GetData` (~250 appels/s, `dpadSeen=0` côté XInput) : neutralisation du hat switch (usage 0x39) dans le proxy pendant que le HUD est ouvert, en plus des hooks XInput/RealVR/IAT. |
| **10/09/2026** | **Frame Guard basé sur SteamVR** | Le compteur NGX n'est pas sur le chemin de Cyberpunk (Streamline) : le guard lit désormais `IVRCompositor::GetFrameTiming` et n'agit qu'après ~1,5 s de tension soutenue. |
| **10/09/2026** | **Processus annexes isolés** | `REDEngineErrorReporter.exe` chargeait aussi le proxy (collision de clé overlay, 3576 erreurs) : les processus auxiliaires ne créent plus d'overlay ni de hooks, clé d'overlay par PID + backoff. |
| **10/09/2026** | **D-Pad hors XInput : revert assumé** | Le pad de test (type DualSense) est lu par Cyberpunk via Raw Input (`GetRawInputData`) et non par XInput. Les tentatives de masquage HID/Raw Input/IAT ont cassé le binding `Select+L3` : tout a été reverté (commit `revert(input)`), le binding repasse par la cible RealVR64 capturée. Le masquage hors XInput est documenté comme limitation connue et reporté. |

---

## 3. Matrice de Tests & Validation Prévue

| Scénario de Test | GPU | Résolution Casque | Configuration DLSS 5 | Objectif Framerate | Statut |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Cyberpunk 2077 Vanilla DLSS 4.5** | RTX 5090 | Quest 3 / Pimax (~4K) | Désactivé | 72 FPS @ 70% GPU | Référence validée |
| **Cyberpunk 2077 + RenoDX DLSS 5** | RTX 5090 | Quest 3 / Pimax (~4K) | Post-SR 4K, Preset 2 | 40 FPS @ 100% GPU | Constat clinique répliqué |
| **Cyberpunk 2077 + OptiScaler Pre-SR** | RTX 5090 | Quest 3 / Pimax (~4K) | Pre-SR, Scale=0.75 | **72 FPS @ ~90% GPU** | Prêt pour test |
| **Cyberpunk 2077 + Pre-SR + AER v2** | RTX 5090 | Quest 3 / Pimax (~4K) | Pre-SR, Scale=0.75 | **72 FPS @ ~46% GPU** | Prêt pour test |
| **Cyberpunk 2077 + Ray Reconstruction** | RTX 5090 | Quest 3 / Pimax (~4K) | Pre-SR + ResidualAcrossRR | **72 FPS stable** | Prêt pour test |
| **Cyberpunk 2077 + HUD live (Detail/Style, toggle, D-Pad HID)** | RTX 5090 | Quest 3 | Pre-SR, contrôle live via canal partagé | **72 FPS stable** | Validé en cours de session |

---

## 4. Release v1.1.0 — Contenu

- Fork OptiScaler wilsjo2 v0.7.6 rebuildé avec canal de contrôle live (`deps/OptiScaler.dll`).
- HUD 7 lignes : Neural Engine, **DLSS5 Detail** (Intensity 0→2), **DLSS5 Style**, WorkingScale, Preset, Placement, VR HUD Display (position/scale). Ray Reconstruction sur `F8`, plus de raccourci `R3`.
- Isolation D-Pad **XInput uniquement** : hooks `XInputGetState`/`joyGetPosEx` + patch single-slot du thunk RealVR64 (cible capturée réutilisée par le watcher, ce qui rend `Select+L3` fiable). Le masquage Raw Input / parseur HID (pads type DualSense lus hors XInput) a été prototypé puis **reverté** : il cassait le binding et reste à reprendre dans une passe dédiée.
- Frame Guard SteamVR (`IVRCompositor::GetFrameTiming`) avec pilotage live du `WorkingScale` et hystérésis ~1,5 s.
- Correctifs robustesse : quarantaine du moteur concurrent `WINMM.dll`, processus annexes ignorés, overlay par PID avec backoff, `WorkingScale` par défaut 0.75.
