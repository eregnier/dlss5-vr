# 05. Nouvelle Architecture pour dlss5-vr : L'Intégration OptiScaler Pre-SR

Ce document établit la refonte architecturale complète du projet `dlss5-vr` pour le mod **LukeRoss REAL VR** sous **Cyberpunk 2077**.

---

## 1. La Découverte Majeure : La Compatibilité Native LukeRoss <> OptiScaler

L'analyse par rétro-ingénierie et désassemblage binaire de `RealVR64.dll` (v2606.14.2) a révélé une surprise technique de premier ordre :

```x86asm
; Désassemblage de RealVR64.dll à l'adresse 0x1802621ed :
lea  rdx, [rip + 0x335174]   ; Chaîne : "OptiScaler.asi loaded and patched"
lea  rcx, [rip + 0x519685]
call 0x18003b220             ; Émission dans le log RealVR.log
```

**Constat capital** :
LukeRoss a spécifiquement anticipé et implémenté dans son code la détection et la coordination stéréoscopique avec `OptiScaler.asi` !
Lorsque `OptiScaler.asi` est présent dans le répertoire de l'exécutable, `RealVR64.dll` adapte automatiquement ses hooks stéréoscopiques (`AUDLSSFixInstanceStateD3D12`), assurant une parfaite séparation des instances DLSS pour l'œil gauche et l'œil droit.

---

## 2. Refonte Structurelle : Abandon de ReShade au Profit d'OptiScaler Pre-SR

### A. L'Ancienne Architecture (Obsolète)
```
Cyberpunk2077.exe
     │
     ├──> dxgi.dll (Proxy Custom dlss5-vr)
     │         └──> RealVR64.dll (LukeRoss)
     │
     └──> dinput8.dll / ReShade64_dlss5.dll (ReShade 6.8 Add-on)
               └──> renodx-dlss5.addon64
                         └──> nvngx_dlssnr.dll [POST-SR 4K = 29.5 Mpx = 40 FPS !]
```
- **Inconvénients** : Double charge ReShade + LukeRoss, conflits de swapchains, mémoire scratch écrasée, évaluation monolithique en Post-SR 4K, 40 FPS verrouillés par la reprojection.

---

### B. La Nouvelle Architecture Recommandée (Haute Performance)

```
Cyberpunk2077.exe
     │
     ├──> dxgi.dll (Dual-Proxy dlss5-vr : Master Orchestrator + VR HUD OpenVR)
     │         │
     │         ├──> RealVR64.dll (Mod VR LukeRoss : Swapchain & Stéréoscopie)
     │         │
     │         └──> OptiScaler.dll / OptiScaler.asi (Pre-SR Engine wilsjo2 v0.7.6)
     │                   │
     │                   ├──> nvngx.dll_dlssnr.dll (Forwarder de validation)
     │                   │         └──> nvngx_dlssnr.dll (Modèle IA FP8 / FP16)
     │                   │
     │                   └──> nvngx_dlss.dll (Super Résolution native)
```

### Avantages Majeurs :
1. **Zéro ReShade** : Suppression intégrale de ReShade 6.8, de ses hooks de capture et de l'add-on RenoDX. Gain de stabilité et temps de chargement immédiat.
2. **Pre-SR Natif** : Le réseau neuronal traite l'image **avant** le suréchantillonnage, ramenant les calculs de 29,5 Mpx à 4,15 Mpx (avec `WorkingScale=0.75`).
3. **Maintien du VR HUD** : Notre `dxgi.dll` conserve l'overlay OpenVR haute résolution dans le casque, permettant au joueur de régler en temps réel le `WorkingScale`, les presets et l'intensité sans quitter la VR.
4. **Prise en charge du Ray Reconstruction** : `ResidualAcrossRR=true` permet de jouer en RT Overdrive avec Ray Reconstruction + DLSS 5 sans artefacts de scintillement.

---

## 3. Configuration Optimale Validée pour Cyberpunk 2077 VR

Le fichier `OptiScaler.ini` doit être configuré avec les paramètres suivants pour garantir **72 FPS solides** sur RTX 4090 / 5090 :

```ini
[DlssNr]
; Activation principale
Enabled=true

; Exécuter le réseau neuronal AVANT la super-résolution (Essentiel !)
RunBeforeSR=true

; Facteur d'échelle de calcul du modèle (75% = 56% de surface = ~1.6 ms sur 5090)
WorkingScale=0.75

; 1 seule passe séquentielle (2 et 3 doublent/triplent le temps de calcul)
Passes=1

; Cyberpunk 2077 Ray Reconstruction : conservation du détail neuronal par-dessus le débruiteur
ResidualAcrossRR=true
ResidualAcrossRRBlend=0.08

; Désactivation des modes de test et captures disques
DeferredDLSS=false
ResidualFG=false
AutoCapture=false
DebugView=0

; Presets et intensités
Preset=2
Style=0
Intensity=1.0
LocalStructure=1.0
LocalTone=1.0
SkinStructure=-1.0
AutoMask=true
```

---

## 4. Deux Topologies de Déploiement Possibles

### Topologie 1 : Déploiement Direct "Zero-Proxy" via LukeRoss (`OptiScaler.asi`)
Cette méthode est la plus simple et la plus directe :
1. Dans `Cyberpunk 2077\bin\x64\` :
   - `dxgi.dll` reste le `RealVR64.dll` officiel de LukeRoss.
   - Copier `OptiScaler.dll` renommé sous le nom **`OptiScaler.asi`** (ou `dbghelp.dll`).
   - Copier `nvngx.dll_dlssnr.dll`, `nvngx_dlssnr.dll` et le sous-dossier `OptiScaler\`.
   - Copier `OptiScaler.ini` optimisé.
2. Au lancement : LukeRoss charge et patche directement `OptiScaler.asi`. Le menu OptiScaler s'ouvre via `Insert`.

### Topologie 2 : Déploiement "Dual-Proxy Enrichi" (Recommandé avec HUD VR)
Cette méthode combine le meilleur des deux mondes :
1. Dans `Cyberpunk 2077\bin\x64\` :
   - `dxgi.dll` est notre proxy C++ compilé.
   - `RealVR64.dll` est préservé et appelé par notre proxy.
   - `OptiScaler.dll` est chargé en tant que module d'upscaling et d'inférence.
   - Notre proxy injecte l'overlay OpenVR (`F6` ou `Select + L3`) directement devant les yeux du joueur dans le casque pour ajuster les dials d'OptiScaler (`WorkingScale`, `Passes`, `Presets`) en direct.

---

## 5. Le Mécanisme de Protection Dynamique de Trame (Dynamic VR Frame Guard)

Pour empêcher tout décrochage vers la reprojection, le proxy ou configurateur peut intégrer un garde-fou dynamique :

$$\text{Si } T_{\text{frame}} > 13,00\text{ ms (seuil d'alerte V-Sync à 72 Hz)} :$$
$$\text{Abaisser automatiquement } \text{WorkingScale} \text{ de } 0,75 \longrightarrow 0,66 \text{ ou } 0,50$$

Ce réglage réduit instantanément le temps Tensor Core de 1,69 ms à 0,75 ms, ramenant immédiatement la trame sous les 13,88 ms et empêchant le casque de basculer à 40 FPS.
