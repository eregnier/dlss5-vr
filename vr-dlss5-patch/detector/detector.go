package detector

import (
	"bytes"
	"debug/pe"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strings"
)

// GameInfo contient les résultats de l'analyse d'un répertoire de jeu.
type GameInfo struct {
	ExePath        string
	GameDir        string
	ExeName        string
	Bitness        int      // 32 ou 64
	Imports        []string // DLLs importées par l'exécutable
	HasDLSS        bool     // Détection d'un DLSS natif
	DLSSPath       string
	HasLukeRoss    bool   // Détection du mod VR LukeRoss
	LukeRossDxgi   bool   // true si dxgi.dll appartient à LukeRoss
	ProxyTarget    string // Nom cible pour ReShade (dinput8.dll, d3d12.dll, d3d11.dll, etc.)
	ConflictReason string // Explication si un nom a été évité pour cause de conflit
	NeedsBridge    bool   // DX11 avec DLSS natif nécessite le bridge
	DetectedAPI    string // DX11, DX12, ou Unknown
	IsReEngine     bool
}

// FindGameExe localise l'exécutable principal du jeu dans un dossier ou valide le chemin fourni.
func FindGameExe(targetPath string) (string, error) {
	fi, err := os.Stat(targetPath)
	if err != nil {
		return "", fmt.Errorf("chemin introuvable: %w", err)
	}

	if !fi.IsDir() {
		if strings.EqualFold(filepath.Ext(targetPath), ".exe") {
			return filepath.Abs(targetPath)
		}
		return "", fmt.Errorf("%s n'est pas un fichier exécutable", targetPath)
	}

	// C'est un dossier: chercher le meilleur .exe
	var candidates []string
	err = filepath.Walk(targetPath, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return nil
		}
		rel, _ := filepath.Rel(targetPath, path)
		if strings.Count(rel, string(os.PathSeparator)) > 2 {
			if info.IsDir() {
				return filepath.SkipDir
			}
			return nil
		}
		if !info.IsDir() && strings.EqualFold(filepath.Ext(path), ".exe") {
			base := strings.ToLower(filepath.Base(path))
			if strings.Contains(base, "crash") ||
				strings.Contains(base, "report") ||
				strings.Contains(base, "setup") ||
				strings.Contains(base, "install") ||
				strings.Contains(base, "unitycrash") ||
				strings.Contains(base, "unrealcef") ||
				strings.Contains(base, "epicgames") ||
				strings.Contains(base, "steam") ||
				strings.Contains(base, "vcredist") ||
				strings.Contains(base, "dxsetup") ||
				strings.Contains(base, "vdesync") ||
				strings.Contains(base, "elevate") ||
				strings.Contains(base, "rxrepl") {
				return nil
			}
			candidates = append(candidates, path)
		}
		return nil
	})
	if err != nil {
		return "", err
	}

	if len(candidates) == 0 {
		return "", fmt.Errorf("aucun exécutable de jeu trouvé dans %s", targetPath)
	}

	for _, c := range candidates {
		if strings.Contains(strings.ToLower(filepath.Base(c)), "shipping") {
			return filepath.Abs(c)
		}
	}

	var best string
	var maxSz int64
	for _, c := range candidates {
		if fi, err := os.Stat(c); err == nil && fi.Size() > maxSz {
			maxSz = fi.Size()
			best = c
		}
	}

	if best != "" {
		return filepath.Abs(best)
	}
	return filepath.Abs(candidates[0])
}

// Inspect analyse l'exécutable et le dossier du jeu pour déterminer la configuration requise.
func Inspect(exePath string) (*GameInfo, error) {
	absExe, err := filepath.Abs(exePath)
	if err != nil {
		return nil, err
	}
	gameDir := filepath.Dir(absExe)
	exeName := filepath.Base(absExe)

	info := &GameInfo{
		ExePath: absExe,
		GameDir: gameDir,
		ExeName: exeName,
		Bitness: 64,
	}

	// 1. Analyse PE de l'exécutable
	if peFile, err := pe.Open(absExe); err == nil {
		defer peFile.Close()
		switch peFile.Machine {
		case pe.IMAGE_FILE_MACHINE_AMD64:
			info.Bitness = 64
		case pe.IMAGE_FILE_MACHINE_I386:
			info.Bitness = 32
		}
		if libs, err := peFile.ImportedLibraries(); err == nil && len(libs) > 0 {
			for _, lib := range libs {
				info.Imports = append(info.Imports, strings.ToLower(lib))
			}
		}
	}
	if len(info.Imports) == 0 {
		info.Imports = scanDllImports(absExe)
	}

	// 2. Détection du DLSS natif
	info.HasDLSS, info.DLSSPath = detectDLSS(gameDir)

	// 3. Détection de LukeRoss VR Mod
	info.HasLukeRoss, info.LukeRossDxgi = detectLukeRoss(gameDir)

	// 4. Détection API graphique
	info.DetectedAPI = detectGraphicsAPI(info.Imports, gameDir)

	// 5. Détection RE Engine
	if _, err := os.Stat(filepath.Join(gameDir, "re_chunk_000.pak")); err == nil {
		info.IsReEngine = true
	}

	// 6. Choix de la cible ReShade avec détection de conflit universelle
	if info.HasLukeRoss {
		// Le mod VR LukeRoss occupe dxgi.dll.
		// Vérifions si dinput8.dll existe déjà dans le dossier et si c'est un autre mod
		dinputExisting := filepath.Join(gameDir, "dinput8.dll")
		dinputIsForeignMod := false

		if fi, err := os.Stat(dinputExisting); err == nil && fi.Size() > 0 {
			if !isReShadeDll(dinputExisting) {
				dinputIsForeignMod = true
				info.ConflictReason = "dinput8.dll appartient déjà à un autre mod (ex: REFramework, ScriptHook) -> préservé"
			}
		}

		hasDinput8Import := hasImport(info.Imports, "dinput8.dll")
		hasD3D12Import := hasImport(info.Imports, "d3d12.dll")
		hasD3D11Import := hasImport(info.Imports, "d3d11.dll")

		// Cascade intelligente :
		// Si dinput8 est importé et libre -> dinput8.dll
		// Sinon si DX12 -> d3d12.dll (tous les jeux DX12 chargent d3d12.dll)
		// Sinon si DX11 -> d3d11.dll (tous les jeux DX11 chargent d3d11.dll)
		// Sinon repli sur dinput8.dll
		if hasDinput8Import && !dinputIsForeignMod {
			info.ProxyTarget = "dinput8.dll"
		} else if info.DetectedAPI == "DX12" || hasD3D12Import {
			info.ProxyTarget = "d3d12.dll"
		} else if info.DetectedAPI == "DX11" || hasD3D11Import {
			info.ProxyTarget = "d3d11.dll"
		} else if !dinputIsForeignMod {
			info.ProxyTarget = "dinput8.dll"
		} else {
			info.ProxyTarget = "d3d12.dll"
		}
	} else {
		// Pas de mod VR détecté : ReShade prend sa place standard en dxgi.dll
		info.ProxyTarget = "dxgi.dll"
	}

	// 7. Vérification du besoin de Bridge DX11
	if info.HasDLSS && info.DetectedAPI == "DX11" {
		info.NeedsBridge = true
	}

	return info, nil
}

func hasImport(imports []string, target string) bool {
	for _, imp := range imports {
		if strings.EqualFold(imp, target) {
			return true
		}
	}
	return false
}

func isReShadeDll(path string) bool {
	data, err := os.ReadFile(path)
	if err != nil || len(data) < 1024*1024 {
		return false
	}
	return bytes.Contains(data, []byte("ReShade")) && bytes.Contains(data, []byte("crosire"))
}

func detectDLSS(gameDir string) (bool, string) {
	var foundPath string
	_ = filepath.Walk(gameDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return nil
		}
		rel, _ := filepath.Rel(gameDir, path)
		if strings.Count(rel, string(os.PathSeparator)) > 3 {
			if info.IsDir() {
				return filepath.SkipDir
			}
			return nil
		}
		name := strings.ToLower(info.Name())
		if !info.IsDir() {
			if name == "nvngx_dlss.dll" || name == "sl.dlss.dll" {
				foundPath = path
				return io.EOF
			}
		}
		return nil
	})
	return foundPath != "", foundPath
}

func detectLukeRoss(gameDir string) (hasLR bool, hasDxgi bool) {
	if _, err := os.Stat(filepath.Join(gameDir, "RealRepo")); err == nil {
		hasLR = true
	}
	if _, err := os.Stat(filepath.Join(gameDir, "RealConfig.bat")); err == nil {
		hasLR = true
	}
	if _, err := os.Stat(filepath.Join(gameDir, "RealVR.ini")); err == nil {
		hasLR = true
	}

	dxgiPath := filepath.Join(gameDir, "dxgi.dll")
	if data, err := os.ReadFile(dxgiPath); err == nil {
		if bytes.Contains(data, []byte("LukeRoss")) ||
			bytes.Contains(data, []byte("R.E.A.L. VR")) ||
			bytes.Contains(data, []byte("RealVR64")) ||
			bytes.Contains(data, []byte("g_rvrShared")) {
			hasLR = true
			hasDxgi = true
		}
	}
	return hasLR, hasDxgi
}

func detectGraphicsAPI(imports []string, gameDir string) string {
	has12 := false
	has11 := false
	for _, imp := range imports {
		if strings.EqualFold(imp, "d3d12.dll") {
			has12 = true
		}
		if strings.EqualFold(imp, "d3d11.dll") {
			has11 = true
		}
	}
	if has12 {
		return "DX12"
	}
	if has11 {
		return "DX11"
	}
	if _, err := os.Stat(filepath.Join(gameDir, "d3d12core.dll")); err == nil {
		return "DX12"
	}
	return "DX12"
}

func scanDllImports(exePath string) []string {
	f, err := os.Open(exePath)
	if err != nil {
		return nil
	}
	defer f.Close()

	// Lire les premiers 16 Mo de l'exécutable pour capturer les tables d'import
	buf := make([]byte, 16*1024*1024)
	n, _ := io.ReadFull(f, buf)
	buf = buf[:n]

	re := regexp.MustCompile(`(?i)[a-z0-9_\.-]+\.dll`)
	matches := re.FindAll(buf, -1)

	seen := make(map[string]bool)
	var res []string
	for _, m := range matches {
		s := strings.ToLower(string(m))
		if !seen[s] {
			seen[s] = true
			res = append(res, s)
		}
	}
	return res
}
