# 02. Diagnostic Approfondi des Goulets d'Étranglement en VR (Cyberpunk 2077 & Luke Ross)

Ce document présente l'analyse mathématique, matérielle et logicielle des causes de la chute brutale de performances (72 FPS $\rightarrow$ 40 FPS, GPU 70% $\rightarrow$ 100%) lors de l'activation de DLSS 5 en réalité virtuelle.

---

## 1. La Réalité Mathématique du Rendu VR Haute Résolution

### A. Surface de Pixels : Plat 4K vs VR Stéréoscopique
Sur écran plat traditionnel, le rendu 4K Ultra-HD représente :
$$S_{\text{flat 4K}} = 3840 \times 2160 = \mathbf{8\,294\,400\text{ pixels}}$$

En casque de réalité virtuelle haute fidélité (Meta Quest 3 supersamplé ou Pimax Crystal en natif) :
- Résolution cible par œil : $\sim 3840 \times 3840$ pixels.
- Surface par œil : $3840 \times 3840 = 14\,745\,600\text{ pixels}$.
- **Surface totale stéréoscopique par image** :
  $$S_{\text{VR total}} = 14\,745\,600 \times 2 = \mathbf{29\,491\,200\text{ pixels}}$$

Le pipeline graphique VR traite **3,56 fois plus de pixels** que la 4K standard sur écran plat. À 72 Hz, le GPU doit générer et post-traiter plus de **2,12 milliards de pixels chaque seconde**.

---

## 2. La Mécanique de Rupture de la V-Sync VR : L'Effet "Falaise"

### A. Le Budget Temporel Strict
Contrairement aux écrans d'ordinateurs dotés de G-Sync / FreeSync où le rafraîchissement s'adapte frame par frame, un casque VR exige une synchronisation rigoureuse avec les panneaux de micro-écrans pour éviter la cinétose (mal des transports).

| Fréquence Casque | Budget V-Sync Strict ($\Delta t_{\text{budget}}$) | Seuil de Tolérance |
| :--- | :--- | :--- |
| **72 Hz** (Standard Quest 3 / Pimax) | **13,88 ms** | $> 13,88\text{ ms} \rightarrow$ Chute à **36 FPS** |
| **80 Hz** | **12,50 ms** | $> 12,50\text{ ms} \rightarrow$ Chute à **40 FPS** |
| **90 Hz** (Standard Pimax Crystal) | **11,11 ms** | $> 11,11\text{ ms} \rightarrow$ Chute à **45 FPS** |
| **120 Hz** | **8,33 ms** | $> 8,33\text{ ms} \rightarrow$ Chute à **60 FPS** |

### B. Pourquoi 40 FPS et 100% GPU ?
Considérons les mesures réelles relevées sur une **NVIDIA GeForce RTX 5090** sous Cyberpunk 2077 avec le mod LukeRoss (DLSS Performance) :

1. **Rendu du jeu de base (Cyberpunk 2077)** :
   - Temps de frame GPU de base : $\sim 9,7\text{ ms}$.
   - Charge GPU : $\frac{9,7}{13,88} \approx \mathbf{70\%}$.
   - Marge restante : $13,88 - 9,7 = \mathbf{4,18\text{ ms}}$.
   - **Résultat : 72 FPS parfaits et stables.**

2. **Activation de DLSS 5 via RenoDX (Post-SR Monolithique)** :
   - Le modèle `nvngx_dlssnr.dll` pèse 165 Mo et compte des millions de poids FP16/FP8.
   - Le temps d'inférence Tensor Core pour traiter 29,49 millions de pixels est de :
     $$T_{\text{Tensor}} \approx \mathbf{12,0\text{ ms}}$$
   - **Temps total requis par trame** :
     $$T_{\text{total}} = 9,7\text{ ms (moteur)} + 12,0\text{ ms (DLSS 5)} = \mathbf{21,7\text{ ms}}$$

3. **Le Déclenchement de la Reprojection Asynchrone (ASW / Motion Smoothing)** :
   - $21,7\text{ ms} \gg 13,88\text{ ms}$. Le moteur rate systématiquement la fenêtre V-Sync.
   - Le runtime VR (Oculus / SteamVR) verrouille immédiatement la fréquence d'affichage à **la moitié de la fréquence native** :
     $$72\text{ Hz} \longrightarrow \mathbf{36\text{ à }40\text{ FPS}}$$
   - Pour chaque trame réelle calculée en retard (qui prend 21,7 ms), le compositeur injecte une trame synthétisée artificiellement par reprojection spatio-temporelle.
   - Le GPU tourne à 100% car il est constamment en retard et cherche à rattraper le train de trames suivant.
   - **L'effet "dents de scie"** : La charge alterne entre 100% (trame en retard) et 50% (attente de la pose suivante par le compositeur), créant une instabilité perçue désagréable.

---

## 3. Les Goulets d'Étranglement Internes Identifiés au Niveau Binaire

Outre le volume brut de pixels, l'audit binaire et mémoire a identifié plusieurs faiblesses critiques dans l'ancienne implémentation RenoDX + Proxy :

### A. Le Piège du Slot 0 (UAV Hazard Direct3D 12)
- Dans le code désassemblé de `renodx-dlss5.addon64` à l'offset `0xDF64`, l'ancien patch d'urgence forçait l'assignation au `Slot 0` de mémoire scratch.
- En VR, les deux yeux sont soumis consécutivement : l'œil gauche écrivait dans le Slot 0, et l'œil droit écrasait les UAVs du Slot 0 quelques microsecondes plus tard.
- Le pilote NVIDIA D3D12 détectait une collision d'écriture concurrente sans barrière mémoire et déclenchait un **GPU Pipeline Flush complet** (`ExecuteCommandLists` serialization), ruinant tout parallélisme de calcul.

### B. La Sérialisation Inter-Queues (`CommandQueue->Wait`)
- À l'offset `0x31150`, RenoDX attendait le fence de la soumission précédente.
- En VR multi-queues (soumission stéréoscopique de LukeRoss), cette synchronisation mutuelle bloquait le thread de rendu GPU jusqu'à l'achèvement complet de l'autre œil, empêchant l'overlap de rendu.

### C. L'Appel Disque Synchrone sur le Hot-Path
- L'ancien proxy effectuait des écritures et lectures `WritePrivateProfileString` / `GetPrivateProfileString` synchrones directement sur le thread de rendu à 144 Hz (72 Hz × 2 yeux). Chaque accès disque bloquait le CPU de 0,5 à 2 ms, suffisant pour faire basculer la trame au-delà du budget V-Sync.

### D. La Collision des Registres `NRPreset` et `NRStyle`
- Dans la structure interne de RenoDX, l'adresse `0x196B98` gère l'architecture du réseau (`NRPreset`), tandis que `0x196C2C` gère le ton d'image (`NRStyle`).
- L'écriture simultanée modifiait violemment le tone-mapping cinématique tout en laissant l'utilisateur penser que le coût IA variait peu.
- De plus, les presets de réseau neuronal ne sont pris en compte par NGX **qu'à la création de la feature**. Une modification à chaud en jeu n'avait aucun effet sans reconstruction explicite de la ressource.

---

## 4. Conclusion & Solution Architecturale

Tous les éléments convergent vers une conclusion univoque :
- **Il est physiquement et mathématiquement impossible de faire tourner un réseau neuronal de débruitage/reconstruction lourd (165 Mo) en Post-SR à 4K par œil (29,5 Mpx) en respectant un budget de 13,88 ms.**
- **La seule et unique méthode viable consiste à exécuter le réseau neuronal en Pre-SR**, c'est-à-dire **sur le tampon de rendu basse résolution (1920 × 1920 ou inférieur via `WorkingScale`)**, exactement ce que réalise l'architecture de `wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass`.
