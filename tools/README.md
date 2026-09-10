# Tools & Diagnostics (`tools/`)

Ce répertoire regroupe les scripts Python d'analyse, d'instrumentation et d'audit utilisés pour le reverse-engineering du pipeline VR + DLSS 5.

## Utilitaires Python

- **`live_probe.py`** : Sonde live d'une session en cours en une commande (modules, chaînes de hooks `XInputGetState`, IAT du jeu, bloc de contrôle partagé OptiScaler, tail filtré du log proxy ; `--follow` pour suivre en continu). C'est l'outil à utiliser pour diagnostiquer sans relancer le jeu.
- **`diag.py`** : Diagnostic complet de l'environnement de jeu en cours (processus, hooks, offsets de patch en RAM, détection RealVR / RenoDX).
- **`tail_log.py`** : Surveillance en temps réel des journaux d'exécution ReShade et LukeRoss.
- **`analyze_eval.py`** : Analyseur du flux d'évaluation continue de RenoDX et des points d'interception D3D12.
- **`patch_workset.py`** : Script d'analyse et test de patch de la table d'allocation mémoire (`0xDFF5`).
- **`patch_d3d12.py`** : Analyseur de structures de commandes D3D12.

## Dossier `audit/`

Contient les dumps désassemblés, tables d'exports PE et rapports d'audits réalisés lors du développement du proxy :
- `afop_imports.txt`, `sysdxgi_exports.txt`, `realvr_exports.txt` : Tables IAT et EAT.
- `audit_3cc0.txt`, `audit_realvr_eval.txt` : Analyse des instructions de boucle d'évaluation RenoDX.
- `slots_deep_scan.txt`, `eval_map.txt`, `realvr_scan.txt` : Cartographie des pointeurs et slots mémoire.
