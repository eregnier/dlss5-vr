package installer

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"vr-dlss5-patch/detector"
)

const ManifestFile = ".vr-dlss5-patch.manifest"

type InstallPlan struct {
	ReShadeDllPath string
	AddonPath      string
	ModelPath      string
	BridgePath     string
}

// Install applique le patch dans le répertoire du jeu selon l'analyse GameInfo.
func Install(info *detector.GameInfo, plan *InstallPlan, dryRun bool) ([]string, error) {
	var actions []string
	var installedFiles []string

	gameDir := info.GameDir

	// 1. Déploiement de ReShade
	destProxy := filepath.Join(gameDir, info.ProxyTarget)
	if dryRun {
		actions = append(actions, fmt.Sprintf("Copier ReShade -> %s", destProxy))
	} else {
		if err := copyFile(plan.ReShadeDllPath, destProxy); err != nil {
			return nil, fmt.Errorf("erreur déploiement ReShade (%s): %w", info.ProxyTarget, err)
		}
		installedFiles = append(installedFiles, info.ProxyTarget)
		actions = append(actions, fmt.Sprintf("Installé: ReShade en tant que %s", info.ProxyTarget))
	}

	// 2. Déploiement Add-on DLSS 5 (renodx-dlss5.addon64)
	destAddon := filepath.Join(gameDir, "renodx-dlss5.addon64")
	if dryRun {
		actions = append(actions, "Copier renodx-dlss5.addon64")
	} else {
		if err := copyFile(plan.AddonPath, destAddon); err != nil {
			return nil, fmt.Errorf("erreur déploiement renodx-dlss5.addon64: %w", err)
		}
		installedFiles = append(installedFiles, "renodx-dlss5.addon64")
		actions = append(actions, "Installé: renodx-dlss5.addon64")
	}

	// 3. Déploiement Modèle Neuronal (nvngx_dlssnr.dll)
	destModel := filepath.Join(gameDir, "nvngx_dlssnr.dll")
	if dryRun {
		actions = append(actions, "Copier nvngx_dlssnr.dll")
	} else {
		if err := copyFile(plan.ModelPath, destModel); err != nil {
			return nil, fmt.Errorf("erreur déploiement nvngx_dlssnr.dll: %w", err)
		}
		installedFiles = append(installedFiles, "nvngx_dlssnr.dll")
		actions = append(actions, "Installé: nvngx_dlssnr.dll")
	}

	// 4. Déploiement Bridge DX11 si nécessaire
	if info.NeedsBridge && plan.BridgePath != "" {
		destBridge := filepath.Join(gameDir, "dlss5-bridge.addon64")
		if dryRun {
			actions = append(actions, "Copier dlss5-bridge.addon64 (jeu DX11 avec DLSS)")
		} else {
			if err := copyFile(plan.BridgePath, destBridge); err != nil {
				return nil, fmt.Errorf("erreur déploiement dlss5-bridge.addon64: %w", err)
			}
			installedFiles = append(installedFiles, "dlss5-bridge.addon64")
			actions = append(actions, "Installé: dlss5-bridge.addon64")
		}
	}

	// 5. Configuration de ReShade.ini
	if dryRun {
		actions = append(actions, "Configurer ReShade.ini ([RenoDX.DLSS5] NeuralUplift=1, débloquer add-ons)")
	} else {
		if err := updateReShadeIni(gameDir, info.ProxyTarget); err != nil {
			return nil, fmt.Errorf("erreur mise à jour ReShade.ini: %w", err)
		}
		actions = append(actions, "Configuré: ReShade.ini")
	}

	// 6. Neutraliser la détection de conflit entre ReShade 6.8 et le ReShade interne de LukeRoss
	if !dryRun {
		ensureLukeRossCompatibility(gameDir, info.ProxyTarget)
	}

	// 7. Écriture du manifest pour désinstallation propre
	if !dryRun && len(installedFiles) > 0 {
		manifestPath := filepath.Join(gameDir, ManifestFile)
		_ = os.WriteFile(manifestPath, []byte(strings.Join(installedFiles, "\n")), 0644)
	}

	return actions, nil
}

// ensureLukeRossCompatibility neutralise l'export 'ReShadeVersion' dans le dxgi.dll de LukeRoss
// et désactive le hook openvr_api.dll dans le proxy ReShade pour éviter un conflit avec le runtime VR.
func ensureLukeRossCompatibility(gameDir, proxyTarget string) {
	dxgiPath := filepath.Join(gameDir, "dxgi.dll")
	data, err := os.ReadFile(dxgiPath)
	if err == nil {
		needle := []byte("ReShadeVersion\x00")
		if idx := strings.Index(string(data), string(needle)); idx != -1 {
			copy(data[idx:], []byte("ReShxdeVersion\x00"))
			_ = os.WriteFile(dxgiPath, data, 0644)
		}
	}

	// Neutraliser le hook openvr_api.dll dans le proxy ReShade
	proxyPath := filepath.Join(gameDir, proxyTarget)
	if pData, err := os.ReadFile(proxyPath); err == nil {
		openvrWide := []byte("o\x00p\x00e\x00n\x00v\x00r\x00_\x00a\x00p\x00i\x00.\x00d\x00l\x00l\x00")
		openvxWide := []byte("o\x00p\x00e\x00n\x00v\x00x\x00_\x00a\x00p\x00i\x00.\x00d\x00l\x00l\x00")
		if oIdx := strings.Index(string(pData), string(openvrWide)); oIdx != -1 {
			copy(pData[oIdx:], openvxWide)
			_ = os.WriteFile(proxyPath, pData, 0644)
		}
	}
}

// Uninstall supprime les fichiers installés par ce patch d'après le manifest.
func Uninstall(gameDir string) ([]string, error) {
	manifestPath := filepath.Join(gameDir, ManifestFile)
	data, err := os.ReadFile(manifestPath)
	if err != nil {
		return nil, fmt.Errorf("aucun manifest d'installation trouvé dans %s: %w", gameDir, err)
	}

	var removed []string
	lines := strings.Split(string(data), "\n")
	for _, l := range lines {
		f := strings.TrimSpace(l)
		if f == "" || strings.HasPrefix(f, "#") {
			continue
		}
		p := filepath.Join(gameDir, f)
		if err := os.Remove(p); err == nil {
			removed = append(removed, f)
		}
	}

	_ = os.Remove(manifestPath)
	removed = append(removed, ManifestFile)
	return removed, nil
}

// updateReShadeIni configure ReShade.ini pour le DLSS 5 et s'assure qu'aucun add-on n'est désactivé.
func updateReShadeIni(gameDir, proxyTarget string) error {
	iniPath := filepath.Join(gameDir, "ReShade.ini")
	sections := parseIni(iniPath)

	// Section PROXY (pour déléguer au système Windows le module de proxy utilisé)
	if proxyTarget != "" && !strings.EqualFold(proxyTarget, "dxgi.dll") {
		proxy := getOrCreateSection(sections, "PROXY")
		setKey(proxy, "EnableProxyLibrary", "1")
		setKey(proxy, "ProxyLibrary", filepath.Join(`C:\WINDOWS\system32`, proxyTarget))
	}

	// Section GENERAL
	general := getOrCreateSection(sections, "GENERAL")
	setDefault(general, "EffectSearchPaths", `.\reshade-shaders\Shaders\**`)
	setDefault(general, "TextureSearchPaths", `.\reshade-shaders\Textures\**`)
	setDefault(general, "PresetPath", `.\ReShadePreset.ini`)

	// Section RenoDX.DLSS5
	renodx := getOrCreateSection(sections, "RenoDX.DLSS5")
	setKey(renodx, "NeuralUplift", "1")

	// Section ADDON: nettoyer DisabledAddons
	if addonSec := findSection(sections, "ADDON"); addonSec != nil {
		if raw, ok := getKey(addonSec, "DisabledAddons"); ok {
			var kept []string
			for _, item := range strings.Split(raw, ",") {
				t := strings.TrimSpace(item)
				if strings.Contains(strings.ToLower(t), "dlss5") ||
					strings.Contains(strings.ToLower(t), "renodx") ||
					strings.Contains(strings.ToLower(t), "bridge") {
					continue
				}
				if t != "" {
					kept = append(kept, t)
				}
			}
			setKey(addonSec, "DisabledAddons", strings.Join(kept, ","))
		}
	}

	return writeIni(iniPath, sections)
}

// Simple INI handling
type iniSection struct {
	name string
	keys map[string]string
	list []string
}

func parseIni(path string) []*iniSection {
	var sections []*iniSection
	curSec := &iniSection{name: "", keys: make(map[string]string)}
	sections = append(sections, curSec)

	f, err := os.Open(path)
	if err != nil {
		return sections
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, ";") || strings.HasPrefix(line, "#") {
			continue
		}
		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			secName := line[1 : len(line)-1]
			curSec = getOrCreateSection(sections, secName)
			if !containsSection(sections, secName) {
				sections = append(sections, curSec)
			}
			continue
		}
		if idx := strings.Index(line, "="); idx > 0 {
			k := strings.TrimSpace(line[:idx])
			v := strings.TrimSpace(line[idx+1:])
			setKey(curSec, k, v)
		}
	}
	return sections
}

func containsSection(secs []*iniSection, name string) bool {
	for _, s := range secs {
		if strings.EqualFold(s.name, name) {
			return true
		}
	}
	return false
}

func findSection(secs []*iniSection, name string) *iniSection {
	for _, s := range secs {
		if strings.EqualFold(s.name, name) {
			return s
		}
	}
	return nil
}

func getOrCreateSection(secs []*iniSection, name string) *iniSection {
	if s := findSection(secs, name); s != nil {
		return s
	}
	newSec := &iniSection{name: name, keys: make(map[string]string)}
	return newSec
}

func setKey(s *iniSection, key, val string) {
	if _, exists := s.keys[strings.ToLower(key)]; !exists {
		s.list = append(s.list, key)
	}
	s.keys[strings.ToLower(key)] = val
}

func getKey(s *iniSection, key string) (string, bool) {
	v, ok := s.keys[strings.ToLower(key)]
	return v, ok
}

func setDefault(s *iniSection, key, val string) {
	if _, ok := getKey(s, key); !ok {
		setKey(s, key, val)
	}
}

func writeIni(path string, secs []*iniSection) error {
	var sb strings.Builder
	for _, s := range secs {
		if len(s.list) == 0 && s.name == "" {
			continue
		}
		if s.name != "" {
			sb.WriteString(fmt.Sprintf("[%s]\n", s.name))
		}
		for _, k := range s.list {
			v := s.keys[strings.ToLower(k)]
			sb.WriteString(fmt.Sprintf("%s=%s\n", k, v))
		}
		sb.WriteString("\n")
	}
	return os.WriteFile(path, []byte(sb.String()), 0644)
}

func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	_ = os.MkdirAll(filepath.Dir(dst), 0755)
	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, in)
	return err
}
