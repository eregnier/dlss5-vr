package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"vr-dlss5-patch/detector"
	"vr-dlss5-patch/downloader"
	"vr-dlss5-patch/installer"
)

const Version = "1.0.0"

func main() {
	var (
		checkOnly   bool
		remove      bool
		dryRun      bool
		proxyName   string
		cacheDir    string
		targetGame  string
	)

	flag.BoolVar(&checkOnly, "check", false, "Analyser le répertoire et afficher le diagnostic sans modifier de fichier")
	flag.BoolVar(&remove, "remove", false, "Désinstaller tous les composants déposés par cet outil")
	flag.BoolVar(&dryRun, "dry-run", false, "Simuler les étapes d'installation sans écrire de fichier")
	flag.StringVar(&proxyName, "proxy", "", "Forcer le nom de la DLL proxy ReShade (ex: dinput8.dll, d3d12.dll)")
	flag.StringVar(&cacheDir, "cache-dir", "", "Répertoire de cache personnalisé pour les téléchargements")
	flag.StringVar(&targetGame, "game", "", "Chemin vers l'exécutable ou le dossier du jeu")
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "vr-dlss5-patch v%s - Intégrateur LukeRoss REAL VR & DLSS 5 Neural Rendering\n\n", Version)
		fmt.Fprintf(os.Stderr, "Usage:\n")
		fmt.Fprintf(os.Stderr, "  vr-dlss5-patch [options] <chemin_jeu_ou_exe>\n\n")
		fmt.Fprintf(os.Stderr, "Options:\n")
		flag.PrintDefaults()
	}
	flag.Parse()

	args := flag.Args()
	if targetGame == "" && len(args) > 0 {
		targetGame = args[0]
	}

	if targetGame == "" {
		flag.Usage()
		os.Exit(1)
	}

	fmt.Println("================================================================================")
	fmt.Printf(" vr-dlss5-patch v%s - LukeRoss REAL VR + DLSS 5 (Neural Rendering)\n", Version)
	fmt.Println("================================================================================")

	// 1. Détection de l'exécutable
	exePath, err := detector.FindGameExe(targetGame)
	if err != nil {
		fmt.Fprintf(os.Stderr, "\n[ERREUR] %v\n", err)
		os.Exit(1)
	}

	// 2. Analyse de l'environnement du jeu
	info, err := detector.Inspect(exePath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "\n[ERREUR] Échec de l'analyse : %v\n", err)
		os.Exit(1)
	}

	if proxyName != "" {
		info.ProxyTarget = proxyName
	}

	// Affichage du diagnostic
	fmt.Printf("\n[1/3] Diagnostic du jeu :\n")
	fmt.Printf("  • Exécutable     : %s (%d-bit)\n", info.ExePath, info.Bitness)
	fmt.Printf("  • Dossier        : %s\n", info.GameDir)
	fmt.Printf("  • API Détectée   : %s\n", info.DetectedAPI)
	fmt.Printf("  • DLSS Natif     : %s\n", formatBool(info.HasDLSS))
	fmt.Printf("  • Mod LukeRoss VR: %s\n", formatBool(info.HasLukeRoss))

	if info.HasLukeRoss {
		fmt.Printf("    -> Détecté : Le mod VR LukeRoss occupe 'dxgi.dll'.\n")
		if info.ConflictReason != "" {
			fmt.Printf("    -> Protection : %s\n", info.ConflictReason)
		}
		fmt.Printf("    -> Stratégie : ReShade 6.8 sera injecté via '%s' (chaînage sans conflit).\n", info.ProxyTarget)
	} else {
		fmt.Printf("    -> ReShade sera injecté directement en '%s'.\n", info.ProxyTarget)
	}

	if remove {
		fmt.Printf("\n[Désinstallation] Suppression des fichiers installés...\n")
		removed, err := installer.Uninstall(info.GameDir)
		if err != nil {
			fmt.Fprintf(os.Stderr, "[ERREUR] %v\n", err)
			os.Exit(1)
		}
		for _, f := range removed {
			fmt.Printf("  - Supprimé : %s\n", f)
		}
		fmt.Println("\nNettoyage terminé avec succès.")
		return
	}

	if checkOnly {
		fmt.Printf("\nMode --check terminé. Aucun fichier n'a été modifié.\n")
		return
	}

	// 3. Récupération des composants
	fmt.Printf("\n[2/3] Préparation des composants DLSS 5 & ReShade :\n")
	dl, err := downloader.NewDownloader(cacheDir)
	if err != nil {
		fmt.Fprintf(os.Stderr, "[ERREUR] %v\n", err)
		os.Exit(1)
	}

	reshadeDll, err := dl.GetReShadeDll(info.Bitness)
	if err != nil {
		fmt.Fprintf(os.Stderr, "[ERREUR] ReShade: %v\n", err)
		os.Exit(1)
	}
	fmt.Printf("  ✓ ReShade Add-on DLL prêt (%s)\n", filepath.Base(reshadeDll))

	addonPath, err := dl.GetDLSS5Addon()
	if err != nil {
		fmt.Fprintf(os.Stderr, "[ERREUR] DLSS 5 Addon: %v\n", err)
		os.Exit(1)
	}
	fmt.Printf("  ✓ renodx-dlss5.addon64 prêt\n")

	modelPath, err := dl.GetDLSSNRModel()
	if err != nil {
		fmt.Fprintf(os.Stderr, "[ERREUR] DLSS NR Model: %v\n", err)
		os.Exit(1)
	}
	fmt.Printf("  ✓ nvngx_dlssnr.dll prêt\n")

	var bridgePath string
	if info.NeedsBridge {
		bridgePath, err = dl.GetBridgeAddon()
		if err != nil {
			fmt.Fprintf(os.Stderr, "[ERREUR] Bridge DX11: %v\n", err)
			os.Exit(1)
		}
		fmt.Printf("  ✓ dlss5-bridge.addon64 prêt\n")
	}

	// 4. Installation dans le jeu
	fmt.Printf("\n[3/3] Application du patch dans le jeu :\n")
	plan := &installer.InstallPlan{
		ReShadeDllPath: reshadeDll,
		AddonPath:      addonPath,
		ModelPath:      modelPath,
		BridgePath:     bridgePath,
	}

	actions, err := installer.Install(info, plan, dryRun)
	if err != nil {
		fmt.Fprintf(os.Stderr, "[ERREUR] Installation: %v\n", err)
		os.Exit(1)
	}

	for _, a := range actions {
		fmt.Printf("  • %s\n", a)
	}

	fmt.Println("\n================================================================================")
	fmt.Println(" INSTALLATION TERMINÉE AVEC SUCCÈS !")
	fmt.Println("================================================================================")
	fmt.Println("Fonctionnement en jeu :")
	fmt.Println("  1. Lancez le jeu normalement avec votre casque VR allumé.")
	fmt.Println("  2. Le mod LukeRoss s'activera via dxgi.dll (rendu stéréoscopique OpenXR/SteamVR).")
	fmt.Println("  3. ReShade 6.8 s'activera via " + info.ProxyTarget + " et chargera DLSS 5 Neural Rendering.")
	fmt.Println("  4. Raccourcis en jeu :")
	fmt.Println("     - [Home]   : Menu ReShade -> Onglet 'Add-ons' pour vérifier l'état du DLSS 5")
	fmt.Println("     - [F6]     : Activer / Désactiver le Neural Rendering instantanément")
	fmt.Println("     - [PavNum] : Menu de réglages LukeRoss VR")
	fmt.Println("================================================================================")
}

func formatBool(b bool) string {
	if b {
		return "OUI"
	}
	return "NON"
}
