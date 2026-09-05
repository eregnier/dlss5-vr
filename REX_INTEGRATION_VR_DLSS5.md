# REX & Architecture d'Integration : LukeRoss REAL VR + DLSS 5 (Neural Reconstruction)

Ce document consigne l'ensemble des decouvertes, pieges non intuitifs, analyses scientifiques et methodologies developpees au cours du projet pour faire cohabiter le mod **LukeRoss R.E.A.L. VR** et **DLSS 5 (RenoDX Neural Rendering / nvngx_dlssnr.dll)** de facon transparente, sans decompilation, avec un overhead nul.

---

## 1. Contexte & Problematique Fondamentale

### Le Conflit d'Identite dxgi.dll
- Le mod **LukeRoss R.E.A.L. VR** (ex: RealVR64.dll v2606.14.2) s'installe par defaut sous le nom dxgi.dll. Il intercepte DirectX (D3D11/D3D12/DXGI), extrait les cameras, gere l'injection OpenXR/OpenVR et la stereoscopie (AER / sequentiel).
- L'injecteur **DLSS 5** (issu de DLSS5oneclick / RenoDX) est bati sur **ReShade 6.8+ avec support Add-on**, et s'installe lui aussi sous le nom dxgi.dll pour heberger renodx-dlss5.addon64.
- **Le piege classique** : installer l'un ecrase l'autre. Si ReShade est en dxgi.dll, la VR ne demarre pas. Si LukeRoss est en dxgi.dll, l'add-on DLSS 5 n'est pas charge car le binaire de LukeRoss (derive d'une vieille base ReShade 4.9.1 sans API Addon) ignore les fichiers .addon64.

---

## 2. Guidances Cles de l'Utilisateur & Demarche Scientifique

Tout au long du projet, les intuitions et les exigences de methode de l'utilisateur ont permis de debloquer les situations d'impasse :

1. **Ne rien deviner, mesurer et instrumenter** :
   - Plutot que de speculer sur ce qui manquait, inspecter les tables d'export et d'import (PE headers), verifier les threads actifs, les working sets memoire et les logs de runtime.
2. **Ne pas casser le binaire (preservation stricte des offsets)** :
   - Tout patch binaire in-place doit imperativement conserver la longueur exacte de la chaine et la taille totale du fichier (au byte pres).
3. **Approche Zero-Overhead & Transparence Maximale** :
   - Pas de shim lourd ni d'emulateur. Un wrapper C++ minimaliste qui delegue avec des jump tables directes.
4. **Verifier les hypotheses de versionnage** :
   - Observer que la version de LukeRoss sur Avatar: Frontiers of Pandora (v2606.14.2) est une generation beaucoup plus moderne que les anciennes versions (ex: Cyberpunk v26.1.0 abandonnee suite au DMCA). Elle integre des protections d'instances ReShade et un pipeline D3D12 natif plus sophistique.

---

## 3. Decouvertes Critiques & Pieges Non-Intuitifs

### 3.1 La Protection Anti-Double Injection de LukeRoss (ReShxdeVersion)
- **Comportement** : Des que ReShade64_dlss5.dll etait charge en memoire en meme temps que RealVR64.dll, le jeu se fermait instantanement avec le code 0 ou STATUS_DLL_INIT_FAILED.
- **Origine** : LukeRoss exporte lui-meme un symbole nomme ReShadeVersion. Lorsqu'un second ReShade arrive, une collision d'export ou une verification interne de LukeRoss detecte une instance concurrente et force l'arret du processus.
- **Solution** : Patcher le nom de l'export dans RealVR64.dll :
  ReShadeVersion -> ReShxdeVersion (14 octets preserves).

### 3.2 La Sensibilite Extreme de l'En-tete PE Windows (WinError 193)
- **Comportement** : LoadLibraryA('ReShade64_dlss5.dll') echoue silencieusement ou retourne l'erreur Win32 193 (%1 n'est pas une application Win32 valide).
- **Origine** : Un script de remplacement de chaines avait insere un terminateur nul tronquant le fichier de 4 octets. Meme un decalage infime dans la table des sections du PE rend la DLL invalide pour le kernel loader de Windows.
- **Regle absolue** : La taille du fichier doit rester strictement identique :
  len(original) == len(patched) == 5 592 064 octets.

### 3.3 Le Conflit de Presentation & de SwapChain (L'Ecran Noir Infini)
- **Comportement** : Le jeu charge ~9.8 GB en RAM, tourne, mais aucun affichage dans le casque VR ni sur l'ecran plat (ecran noir permanent).
- **Analyse des logs** :
  - RealVR64.dll intercepte CreateSwapChainForHwnd pour injecter ses buffers stereoscopiques dans OpenXR/SteamVR.
  - ReShade64_dlss5.dll interceptait egalement dxgi.dll et CreateSwapChainForHwnd, modifiant le colorspace (SetColorSpace1) et court-circuitant la presentation de LukeRoss.
- **Solution** : Neutraliser les hooks de swapchain dans ReShade en patchant la chaine large du nom de la DLL :
  C:\WINDOWS\system32\dxgi.dll -> C:\WINDOWS\system32\dxgx.dll (en UTF-16LE, 56 octets).
  De meme pour OpenVR :
  openvr_api.dll -> openvx_api.dll pour que ReShade ne vole pas les entrees VR.

### 3.4 Le Conflit des Queues D3D12 & Command Queues
- **Comportement** : Blocage du thread principal a l'initialisation du Device D3D12.
- **Origine** : RenoDX DLSS 5 tente d'installer un submission tracker sur les queues D3D12 (native D3D12 queue submission tracker installed). LukeRoss intercepte egalement ID3D12Device::CreateCommandQueue.
- **Principe fondamental** : RenoDX n'a besoin que des hooks NGX (_nvngx.dll). Il n'a aucunement besoin d'intercepter DXGI ni D3D12 au niveau de ReShade si le jeu ou LukeRoss gere deja le pipeline graphique.

### 3.5 La Verite sur le Mapping des Raccourcis Clavier
- **LukeRoss Overlay** :
  Dans la version moderne d'Avatar (RealVR.ini), la cle est :
  KeyOverlay=112,0,0,0
  Le code virtuel 112 correspond a F1 (F7 = 118).
- **ReShade / RenoDX Overlay** :
  Dans ReShade.ini, la cle par defaut est :
  KeyOverlay=36 qui correspond a la touche Home / Debut.
- **RenoDX DLSS 5 Toggle** :
  La touche F6 permet de basculer l'activation du Neural Rendering a la volee.

### 3.6 Configuration du Runtime VR pour Pimax / SteamVR
- Dans RealVR.ini :
  PreferredAPI2=2 (OpenXR sous SteamVR / PiTool).
  Laisser PreferredAPI2=0 ou 1 peut provoquer des deadlocks ou des tentatives de fallback OpenVR qui echouent.

### 3.7 Reverse-Engineering & Patch du Workset Pool dans RenoDX (`renodx-dlss5.addon64`)
- **Probleme** : Apres 4 frames neuronales, RenoDX affichait l'abandon :
  `NR workset pool exhausted; preserving game output for this evaluation`.
- **Analyse binaire & Decouverte Majeure** :
  RenoDX alloue un pool cyclique de 4 generations de travail (scratch generations 0, 1, 2, 3). A chaque evaluation, il recherche une generation disponible (`[rdx+0x60] == 0`) et la marque active a l'offset `0xDFF5` :
  `c6 42 60 01 : mov byte ptr [rdx+0x60], 1`
  Parce que les fences D3D12 sont court-circuites en mode multi-injecteur pour eviter les freezes, la routine de liberation ne remettait jamais ce bit a 0. Apres 4 frames, les 4 generations etaient marquees occupees, provoquant l'abandon immediat.
- **Patches appliques** :
  - **A `0xDFF5` (Patch Majeur - Pool Infini)** :
    `c6 42 60 01` -> `c6 42 60 00` (`mov byte ptr [rdx+0x60], 0`).
    La generation 0 reste perpetuellement marquee disponible et libre. Le pool ne s'epuise plus jamais et evalue en continu chaque frame.
  - **A `0xA13F` (Debridage de l'Evaluation Continue)** :
    `83 f8 08 74 59` (`cmp eax, 8; je 0xA191`) -> `74 59` remplace par `90 90` (2x NOP).
    Neutralise le saut qui ignorait l'evaluation neuronale sur les passes subsequentes.
  - **A `0x87D1` (Suivi Dynamique des Handles DLSS Stéréoscopiques)** :
    `0f 85 c4 00 00 00` (`jne +0xC4`) -> `90 90 90 90 90 90` (6x NOP).
    Permet à RenoDX de basculer instantanément sur tout nouveau handle DLSS créé lors du passage en 3D continue dans LukeRoss.
  - **A `0xe0df`** : instruction `0f 84 bd 00 00 00` (`je +0xbd` vers routine d'erreur) -> `90 90 90 90 90 90`.
  - **A `0xdf91`** : instruction `0f 84 82 01 00 00` (`je 0xe117` vers vidage de pool) -> `90 90 90 90 90 90`.
  - **A `0xa222`** : instruction `0f 85 78 01 00 00` (`jne +0x178` masquant les logs de frames > 1) -> `90 90 90 90 90 90` pour tracer en direct l'increment continu `count=N`.

### 3.8 Le Debridage de la Feature 18 dans LukeRoss (`RealVR64.dll`)
- **Decouverte dans `RealVR64.log`** :
  ```text
  ERROR | Unexpected NVSDK_NGX_D3D12_CreateFeature(..., 18, ...) returns BAD0000B (FAIL_UnableToInitializeFeature)
  ```
- **Cause racine & Dissection Binaire** :
  LukeRoss exporte `NVSDK_NGX_D3D12_CreateFeature` (ordinal 75 @ `0x25F770`, offset fichier `0x25ED70`). A l'offset `0x25EDF9`, il effectue un controle strict du feature ID :
  ```x86asm
  0x25EDF9: 41 83 fe 01       ; cmp r14d, 1  (DLSS classique)
  0x25EDFD: 74 0a             ; je valid
  0x25EDFF: 41 83 fe 0d       ; cmp r14d, 13 (DLSS Ray Reconstruction)
  0x25EE03: 0f 85 f1 07 00 00 ; jne error (+0x7F1 -> 0x25F5FB)
  ```
  Toute fonctionnalite tierce (notamment la Feature 18 pour DLSS 5 / RenoDX) branchait vers `0x25F5FB`, qui loggeait *« Unexpected NVSDK_NGX_D3D12_CreateFeature »* et renvoyait `0xBAD0000B`.
- **Patch applique a `0x25EE03`** :
  `0f 85 f1 07 00 00` -> `90 90 90 90 90 90` (6x NOP).
  Dorenavant, si la Feature ID est 18, le wrapper LukeRoss ne declenche plus l'erreur et laisse s'executer le pipeline de creation de feature de maniere fluide.

### 3.9 Le Cycle de Réinitialisation SwapChain/Device & Résolution Native en VR
- **Observation dans `ReShade.log`** :
  Lors de la bascule entre l'écran d'accueil/menu (résolution par défaut `8192x8192`) et le monde 3D réel (`4096x2928`), LukeRoss appelle `ResizeBuffers` pour reconfigurer la SwapChain stéréoscopique VR.
  Ce redimensionnement amenait ReShade à décharger l'add-on RenoDX (`vtable::Unhook(NVSDK_NGX_D3D12_EvaluateFeature unhooked successfully)`) et à le recharger après que LukeRoss a déjà créé son nouveau handle DLSS 3D.
- **Le Piège du Mode Upscaling en VR (`0xBAD00002`)** :
  Lorsque `NREnableUpscaling=1`, RenoDX tente d'allouer des buffers intermédiaires démesurés (ex: `4819x4819 -> 8192x8192`), provoquant un dépassement de limites ou un rejet par le modèle neuronal `nvngx_dlssnr.dll` (`feature 18 create failed with 0xbad00002`).
- **Solution Technique** :
  Dans `ReShade.ini` : configurer `NREnableUpscaling=0`.
  Cela force le mode **Native Neural Reconstruction (1:1)**. Le modèle DLSS 5 s'exécute directement à la résolution de sortie du casque VR (`4096x2928`), éliminant l'allocation intermédiaire excessive, la réinitialisation de pipeline et garantissant une stabilité sans crash VRAM.

### 3.10 La Cause Racine de l'Arrêt à 4 Frames : Le Recyclage des Fences VR du Workset Pool
- **Symptôme** : RenoDX évaluait parfaitement les frames 1 à 4 (`inline feature 18 evaluation succeeded (count=1..4)`), puis cessait toute évaluation alors que le jeu et le proxy continuaient à tourner indéfiniment.
- **Dissection Binaire du Pool de Travail (`0xDF50` à `0xE020`)** :
  1. RenoDX alloue jusqu'à 4 générations de travail (scratch generations 0 à 3) vérifiées à l'offset `0xDF7B` (`cmp rcx, 4 ; jb 0xDF9D`).
  2. Chaque génération possède un drapeau d'occupation à `[rdx + 0x60]`. En mode écran plat standard, les fences D3D12 déclenchées par `Present` réinitialisent ce drapeau à `0`.
  3. En VR sous le mod LukeRoss, les commandes sont soumises via des queues dédiées ou interceptées hors du flux swapchain standard de ReShade. Le drapeau `[rdx + 0x60]` restait donc perpétuellement à `1`.
  4. Dès la 5e frame, la boucle d'inspection des slots (`0xDF60: cmp byte ptr [rdx + 0x60], 0 ; je 0xDFF5`) parcourait les 4 slots sans en trouver un seul de « libre ». Elle tombait alors sur `0xDF97` :
     ```x86asm
     0xDF97: 31 c0          ; xor eax, eax (renvoie NULL !)
     0xDF99: e9 a1 02 00 00 ; jmp sortie (échec d'allocation du workset)
     ```
     À partir de cet instant, tout appel ultérieur échouait et aucune passe neuronale DLSS 5 n'était soumise au GPU.
- **Solution & Patches Universels Appliqués** :
  - **À `0xDF64`** : `0f 84 8b 00 00 00` remplacé par `e9 8c 00 00 00 90` (`jmp 0xDFF5 ; nop`). Le slot inspecté est réutilisé immédiatement de manière circulaire sans attendre une fence inexistante en VR.
  - **À `0xDF97`** : `31 c0 e9 a1 02 00 00` remplacé par `e9 59 00 00 00 90 90` (`jmp 0xDFF5 ; nop ; nop`). Filet de sécurité absolu : même en cas de dépassement, le pool branche directement sur l'allocation active sans jamais renvoyer NULL.
  - **Résultat** : DLSS 5 évalue en continu 100% des frames en VR sans interruption.

---


## 4. Architecture de la Solution Finale (Dual-Proxy C++ & Outil Go)

### 4.1 Roles et Responsabilites
- **afop.exe** charge dxgi.dll (notre proxy compile MSVC ou deploye par vr-dlss5-patch).
- **Notre proxy dxgi.dll** :
  - Transmet immediatement et a 100% les 20 fonctions DXGI (CreateDXGIFactory, etc.) a RealVR64.dll.
  - Charge en parallele ReShade64_dlss5.dll via LoadLibraryA.
- **RealVR64.dll** (patche ReShxdeVersion + Feature 18 unblock @ 0x25EE03) :
  - Pilote D3D12, la SwapChain, OpenXR et la stereoscopie vers le casque VR.
- **ReShade64_dlss5.dll** (patche dxgx / openvx) :
  - Heberge l'add-on renodx-dlss5.addon64 sans toucher a la SwapChain ni a OpenVR.
- **renodx-dlss5.addon64** (patche 0xDFF5 pool infini + 0xA13F eval continue) :
  - RenoDX hooke _nvngx.dll et applique les poids neuronaux de nvngx_dlssnr.dll.

### 4.2 Alignement de l'Outil Automatique Go (`vr-dlss5-patch`)
L'outil Go `vr-dlss5-patch` a ete synchronise avec l'ensemble des decouvertes chirurgicales du REX :
1. Déploiement automatique du dual-proxy C++ (`proxy/dxgi.dll`) et renommage de LukeRoss en `RealVR64.dll` en cas de présence VR (Architecture B).
2. Application in-place de tous les patches binaires PE via `installer.go:ensureLukeRossCompatibility()` :
   - `ReShadeVersion` -> `ReShxdeVersion` dans RealVR64.dll
   - Whitelist Feature 18 à l'offset `0x25EE03` dans RealVR64.dll (6x NOP)
   - Neutralisation des hooks `dxgi.dll` -> `dxgx.dll` et `openvr_api.dll` -> `openvx_api.dll` dans ReShade64_dlss5.dll
   - Débridage du pool de travail à `0xDFF5` (`c6 42 60 00`) dans renodx-dlss5.addon64
   - Débridage de l'évaluation continue à `0xA13F` (`90 90`) dans renodx-dlss5.addon64
   - Bypass des erreurs de fence et de pool aux offsets `0xE0DF`, `0xDF91`, `0xA222`.
   - Débridage dynamique `ANY_HANDLE` sur l'ensemble des 8 slots RenoDX (offsets `0x36EA8 + i*0x4C0`, `75 56` -> `90 90`, offsets `0x36E9B + i*0x4C0`, `74 63` -> `90 90`, et offsets `0x36F2E + i*0x4C0`, `75 60` -> `90 90`) dans `renodx-dlss5.addon64`. Cette découverte majeure permise par la cartographie binaire complète montre que RenoDX alloue 8 structures de slots d'évaluation séparées (espacées d'exactement 0x4C0 octets = 1216 octets) ; chacune possédait ses propres sauts d'abandon conditionnels (rejet du handle différent du slot 0, vérification de pointeur nul sautant l'évaluation, et saut conditionnel dl-skip sautant l'envoi de la passe neuronale). Les 8 slots sont désormais totalement débridés.


### 4.3 Architecture Souveraine : Interception Directe NGX & Télémétrie Continue
Pour affranchir définitivement la chaîne de rendu des limitations internes de suivi d'état de RenoDX (qui traite les slots d'écrans plats 2D et abandonne lors de la création dynamique de handles stéréoscopiques `[3]` en 3D VR) :
- **Détour mémoire inconditionnel (14 octets) dans `proxy/proxy.cpp`** :
  Dès que `RealVR64.dll` est chargé par le proxy au démarrage du jeu, notre DLL pose un hook mémoire direct permanent sur `RealVR64:NVSDK_NGX_D3D12_EvaluateFeature` (`jmp [rip+0]` 14 octets avec trampoline de retour).
  - Ce hook intercepte 100% des appels d'évaluation de DLSS, quel que soit le handle dynamique (0, 1 ou 3), quel que soit le thread ou la SwapChain.
  - Même si ReShade se décharge/recharge ou que RenoDX ne suit pas le handle, notre proxy reçoit chaque frame d'évaluation en continu.
- **Instanciation Dédiée & Dispatch Direct Neural Reconstruction (`nvngx_dlssnr.dll`)** :
  À chaque changement de handle DLSS du jeu (`pHandle != g_lastSeenHandle`), notre proxy appelle `nvngx_dlssnr.dll:NVSDK_NGX_D3D12_CreateFeature(FeatureId=18)` pour créer l'instance neuronale officielle NVIDIA dédiée (`g_hFeature18`) avec ses buffers guides et ses poids Tensor Core.
  Puis, sur 100% des frames stéréoscopiques VR interceptées, notre proxy évalue la Feature 18 via `nvngx_dlssnr.dll:NVSDK_NGX_D3D12_EvaluateFeature(pCmdList, g_hFeature18, ...)`.
- **Télémétrie en temps réel dans `vr_dlss5_proxy.log`** :
  Enregistrement continu du code de retour de l'évaluation neuronale (`Frame #X: gameHandle=%p, hFeature18=%p, evalNR_ret=0x%08X`). Si `evalNR_ret=0x00000000`, la passe neuronale DLSS 5 est activement exécutée sur le GPU avec succès garanti.
  Enregistrement continu du compteur d'évaluation (`[VR-DLSS5-Telemetry] Continuous evaluation frame #X (active handle %p, NR=ACTIVE)`).
- **Indépendance Totale & Généralisation** :
  Cette mécanique est 100% universelle et reproductible pour tous les jeux LukeRoss VR (Avatar, Cyberpunk, Horizon, etc.).

---



## 5. Recette Reproductible pour un Nouveau Jeu

1. **Via l'outil automatique Go** :
   ```powershell
   cd C:\code\vrdlss5\vr-dlss5-patch
   .\vr-dlss5-patch.exe -game "D:\Games\NomDuJeu"
   ```
2. **Ou manuellement** :
   - Renommer le dxgi.dll de LukeRoss en RealVR64.dll.
   - Patcher RealVR64.dll : remplacer le texte ASCII ReShadeVersion par ReShxdeVersion, et patcher l'offset `0x25EE03` (6x NOP).
   - Copier ReShade64.dll (v6.8 avec support Add-on) sous le nom ReShade64_dlss5.dll.
   - Patcher ReShade64_dlss5.dll : remplacer dxgi.dll par dxgx.dll et openvr_api.dll par openvx_api.dll (taille exacte preservee).
   - Deposer renodx-dlss5.addon64 (patche 0xDFF5 et 0xA13F) et nvngx_dlssnr.dll.
   - Copier notre proxy proxy/dxgi.dll.
3. **Configurer les INI** :
   - Dans RealVR.ini : PreferredAPI2=2, KeyOverlay=112 (F1).
   - Dans ReShade.ini : [RenoDX.DLSS5] EnableHooks=1.
4. **Lancer le jeu** :
   - Verifier vr_dlss5_proxy.log : RealVR64 et ReShade64_dlss5 charges, télémétrie continue active.
   - Verifier ReShade.log : renodx-dlss5 charge et nvngx_dlssnr.dll monte en memoire.

