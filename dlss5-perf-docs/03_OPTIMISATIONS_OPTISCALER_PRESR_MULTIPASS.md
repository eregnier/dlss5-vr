# 03. Analyse Technique Approfondie d'OptiScaler Pre-SR Multipass (wilsjo2)

Ce document analyse les mécanismes internes développés par **wilsjo2** dans son fork `OptiScaler-DLSSNR-PreSR-Multipass`. Ce projet apporte les réponses fondamentales aux limitations structurelles des anciens add-ons ReShade/RenoDX.

---

## 1. Le Cœur de l'Innovation : Le Placement Pre-SR (`RunBeforeSR=true`)

### A. Principe du Placement
Dans une architecture graphique utilisant la Super-Résolution (DLSS SR, FSR, XeSS) :
```
[Pipeline Standard Monolithique (RenoDX)] :
Rendu Moteur (1080p) ──> DLSS Super Resolution ──> 4K Display ──> [DLSS 5 Neural Recon] (Lourd !)

[Pipeline Pre-SR OptiScaler (wilsjo2)] :
Rendu Moteur (1080p) ──> [DLSS 5 Neural Recon] (Ultra Léger !) ──> DLSS Super Resolution ──> 4K Display
```

### B. Implémentation dans le Code Source (`DlssNr_Dx12.cpp`)
OptiScaler intercepte l'appel NGX `NVSDK_NGX_D3D12_EvaluateFeature` et s'insère à la fois **avant** et **après** le suréchantillonnage :

```cpp
// Extrait de OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp (Lignes 1180-1194)
if (nrUpscale)
    DlssNr::EvaluateBeforeUpscale(InCmdList, InParameters, nullptr, 0, rayReconstruction);

NVSDK_NGX_Result result =
    NVNGXProxy::D3D12_EvaluateFeature()(InCmdList, InFeatureHandle, InParameters, InCallback);

if (result == NVSDK_NGX_Result_Success && nrUpscale)
    DlssNr::EvaluateAfterUpscale(InCmdList, InParameters, nullptr, rayReconstruction);
```

Lorsque `RunBeforeSR=true` est activé dans `OptiScaler.ini` :
1. `EvaluateBeforeUpscale` extrait le tampon couleur non upscalé (`NVSDK_NGX_Parameter_Color`).
2. Le modèle neural DLSS 5 est exécuté directement sur ce tampon de rendu natif basse résolution.
3. `NVNGXProxy::D3D12_EvaluateFeature` prend le résultat reconstruit par l'IA et l'agrandit jusqu'à la résolution cible du casque.
4. `EvaluateAfterUpscale` ne ré-exécute pas de passe redondante (sauf en mode résidu RR).

---

## 2. Le Réducteur Quadratique de Surface (`WorkingScale`)

### A. La Loi Mathématique en $O(r^2)$
Le coût de calcul des réseaux de neurones convolutionnels (CNN / Transformers visuels) sur les cœurs Tensor est directement proportionnel au nombre total de pixels d'entrée (la surface) :
$$\text{Charge GPU} \propto W \times H = r^2 \cdot (W_{\text{base}} \times H_{\text{base}})$$

| WorkingScale | Résolution par Œil (sur base 1920×1920) | Surface Stéréo | Coût Tensor Core (5090) | Gain Relatif |
| :--- | :--- | :--- | :--- | :--- |
| **1.00** (Natif Pre-SR) | $1920 \times 1920$ | 7,37 Mpx | **3,00 ms** | $-75\%$ vs 4K |
| **0.75** (Équilibré) | $1440 \times 1440$ | 4,15 Mpx | **1,69 ms** | **$-86\%$ vs 4K** |
| **0.66** (Performance) | $1267 \times 1267$ | 3,21 Mpx | **1,31 ms** | **$-89\%$ vs 4K** |
| **0.50** (Ultra-Perf) | $960 \times 960$ | 1,84 Mpx | **0,75 ms** | **$-94\%$ vs 4K** |

### B. Préservation de la Finesse Visuelle
OptiScaler ne dégrade pas le rendu du jeu :
- L'image de base du jeu et ses vecteurs de mouvement conservent l'intégralité de leur résolution de rendu.
- Seule l'estimation d'éclairage et de reconstruction du modèle neuronal est inférée à résolution fractionnaire, puis réinjectée et filtrée (filtre Lanczos3 ou Catmull-Rom).

---

## 3. Gestion Robuste des Tampons avec Padding (`DlssNr_ActiveColor.h`)

Certains moteurs de jeu, notamment le **REDengine 4 de Cyberpunk 2077**, allouent des textures avec un padding mémoire supérieur à la résolution active (par exemple une allocation $2560 \times 1440$ pour un affichage actif $2558 \times 1439$).

### Le Problème dans les Anciens Injecteurs
Dans les anciens add-ons, envoyer une texture paddée au modèle neuronal provoquait soit un crash mémoire immédiat (`DXGI_ERROR_DEVICE_REMOVED`), soit un décalage spatial des coordonnées UV.

### La Solution wilsjo2
OptiScaler détecte la région active d'origine zéro (`renderWidth`, `renderHeight`, `colorBaseX`, `colorBaseY`) :
```cpp
const auto active = PreSrColorExtent(colorDesc, renderWidth, renderHeight, colorBaseX, colorBaseY);
if (active && (active->width != allocationWidth || active->height != allocationHeight)) {
    // Copie uniquement le sous-rectangle actif vers une texture UAV de travail
    // Exécution du modèle DLSS-NR sur la zone utile
    // Copie du résultat de retour sans corrompre le padding du moteur
}
```

---

## 4. Prise en Charge du Ray Reconstruction dans Cyberpunk 2077 (`ResidualAcrossRR`)

### A. Le Dilemme du Ray Reconstruction (DLSS-D)
Dans Cyberpunk 2077 avec Ray Tracing ou Path Tracing (RT Overdrive) :
- Le tampon de couleur d'entrée est non débruité : il est extrêmement bruité par les rayons monte-carlo.
- Si DLSS 5 est exécuté directement sur cette entrée, il tente de "reconstruire" du bruit brut, ce qui crée des scintillements et des artefacts de luminance.
- Si DLSS 5 est exécuté après Ray Reconstruction, on retombe dans le piège du Post-SR 4K à 29,5 millions de pixels !

### B. L'Approche Innovante `ResidualAcrossRR`
1. Le modèle DLSS-NR est exécuté sur l'entrée de rendu basse résolution.
2. OptiScaler extrait le différentiel signé (le "résidu neuronal") :
   $$\Delta = \text{Image}_{\text{NR}} - \text{Image}_{\text{Base}}$$
3. Le résidu est encodé dans un tampon temporaire RGBA16F.
4. Le moteur de jeu exécute Ray Reconstruction et Super-Résolution normalement sur le tampon non altéré.
5. Une passe de composition ultra-légère réapplique le résidu $\Delta$ sur la sortie débruitée, en le reprojetant via les vecteurs de mouvement temporels avec un facteur d'accumulation configurable (`ResidualAcrossRRBlend=0.08`).
6. **Résultat** : Les détails fins de matière et de réflectance du DLSS 5 sont conservés par-dessus le Ray Reconstruction, tout en conservant le coût infime du Pre-SR !

---

## 5. Le Forwarder de Signature (`nvngx.dll_dlssnr.dll`)

### Pourquoi les injecteurs tiers se faisaient rejeter
Le binaire propriétaire de NVIDIA `nvngx_dlssnr.dll` implémente une vérification stricte de l'environnement appelant :
- À chaque appel API, le runtime remonte la pile d'exécution (stack walk) pour inspecter l'adresse de retour.
- Il résout le module propriétaire de cette adresse.
- Si le chemin d'accès ou le nom du fichier ne contient pas la chaîne `"nvngx.dll"` (le nom du pilote NVIDIA officiel étant `_nvngx.dll`), il renvoie immédiatement l'erreur `NVSDK_NGX_Result_FAIL_PlatformError`.

### L'Architecture du Forwarder
wilsjo2 a développé `nvngx.dll_dlssnr.dll` :
- C'est une DLL intermédiaire open-source portant explicitement `nvngx.dll` dans son nom.
- Elle agit comme une passerelle d'appels (thunk layer) isolée.
- Quand OptiScaler appelle le forwarder, c'est le forwarder qui appelle `nvngx_dlssnr.dll`.
- La vérification de signature du binaire NVIDIA réussit à 100%, sans nécessiter de patch binaire illicite en mémoire !

---

## 6. Synthèse des Bénéfices pour le Projet VR

L'adoption des techniques de `OptiScaler-DLSSNR-PreSR-Multipass` résout l'intégralité des écueils du projet `dlss5-vr` :
1. **Division par 7 de la charge Tensor Core** grâce au Pre-SR ($1440 \times 1440$).
2. **Franchissement du seuil V-Sync (12,89 ms < 13,88 ms)**, garantissant 72 FPS natifs sans reprojection.
3. **Compatibilité complète avec Cyberpunk 2077 Ray Reconstruction** via `ResidualAcrossRR`.
4. **Suppression de ReShade 6.8**, réduisant l'overhead CPU et les risques de crash au démarrage.
