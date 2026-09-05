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

---

## 4. Architecture de la Solution Finale (Dual-Proxy C++)

### 4.1 Roles et Responsabilites
- **afop.exe** charge dxgi.dll (notre proxy compile MSVC).
- **Notre proxy dxgi.dll** :
  - Transmet immediatement et a 100% les 20 fonctions DXGI (CreateDXGIFactory, etc.) a RealVR64.dll.
  - Charge en parallele ReShade64_dlss5.dll via LoadLibraryA.
- **RealVR64.dll** (patche ReShxdeVersion) :
  - Pilote D3D12, la SwapChain, OpenXR et la stereoscopie vers le casque VR.
- **ReShade64_dlss5.dll** (patche dxgx / openvx) :
  - Heberge l'add-on renodx-dlss5.addon64 sans toucher a la SwapChain ni a OpenVR.
  - RenoDX hooke _nvngx.dll et applique les poids neuronaux de nvngx_dlssnr.dll.

---

## 5. Recette Reproductible pour un Nouveau Jeu

1. **Preparer les fichiers dans le dossier du jeu** :
   - Renommer le dxgi.dll de LukeRoss en RealVR64.dll.
   - Patcher RealVR64.dll : remplacer le texte ASCII ReShadeVersion par ReShxdeVersion.
   - Copier ReShade64.dll (v6.8 avec support Add-on) sous le nom ReShade64_dlss5.dll.
   - Patcher ReShade64_dlss5.dll : remplacer dxgi.dll par dxgx.dll et openvr_api.dll par openvx_api.dll (en conservant rigoureusement la taille exacte du fichier).
   - Deposer renodx-dlss5.addon64 et nvngx_dlssnr.dll.
   - Compiler et copier notre proxy proxy/dxgi.dll.
2. **Configurer les INI** :
   - Dans RealVR.ini : PreferredAPI2=2, verifier KeyOverlay (112 pour F1).
   - Dans ReShade.ini : KeyOverlay=36 (Home).
3. **Lancer le jeu** :
   - Verifier vr_dlss5_proxy.log : RealVR64 et ReShade64_dlss5 charges.
   - Verifier ReShade.log : renodx-dlss5 charge et nvngx_dlssnr.dll monte en memoire.
