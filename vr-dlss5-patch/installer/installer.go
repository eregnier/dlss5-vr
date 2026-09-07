package installer

import (
	"bufio"
	"bytes"
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
	DualProxyPath  string // Optionnel: chemin vers le proxy dxgi.dll C++ compilé (Architecture B)
	CudartPath     string // Optionnel: cudart64_12.dll (CUDA 12 runtime)
}

// Install applique le patch dans le répertoire du jeu selon l'analyse GameInfo.
func Install(info *detector.GameInfo, plan *InstallPlan, dryRun bool) ([]string, error) {
	var actions []string
	var installedFiles []string

	gameDir := info.GameDir

	// Architecture B : Si LukeRoss est présent et qu'un proxy C++ est fourni
	if info.HasLukeRoss && plan.DualProxyPath != "" {
		// 1a. Renommer dxgi.dll (LukeRoss) -> RealVR64.dll s'il n'est pas déjà renommé
		oldDxgi := filepath.Join(gameDir, "dxgi.dll")
		realVR64 := filepath.Join(gameDir, "RealVR64.dll")
		if _, err := os.Stat(realVR64); os.IsNotExist(err) {
			if _, errDx := os.Stat(oldDxgi); errDx == nil {
				if dryRun {
					actions = append(actions, "Renommer dxgi.dll (LukeRoss) -> RealVR64.dll")
				} else {
					_ = os.Rename(oldDxgi, realVR64)
					installedFiles = append(installedFiles, "RealVR64.dll")
					actions = append(actions, "Renommé: dxgi.dll (LukeRoss) -> RealVR64.dll")
				}
			}
		}

		// 1b. Déployer ReShade 6.8 sous le nom ReShade64_dlss5.dll
		destReShade := filepath.Join(gameDir, "ReShade64_dlss5.dll")
		if dryRun {
			actions = append(actions, "Copier ReShade -> ReShade64_dlss5.dll")
		} else {
			if err := copyFile(plan.ReShadeDllPath, destReShade); err != nil {
				return nil, fmt.Errorf("erreur déploiement ReShade64_dlss5.dll: %w", err)
			}
			installedFiles = append(installedFiles, "ReShade64_dlss5.dll")
			actions = append(actions, "Installé: ReShade en tant que ReShade64_dlss5.dll")
		}

		// 1c. Déployer le proxy C++ en dxgi.dll
		destProxy := filepath.Join(gameDir, "dxgi.dll")
		if dryRun {
			actions = append(actions, "Copier Dual-Proxy C++ -> dxgi.dll")
		} else {
			if err := copyFile(plan.DualProxyPath, destProxy); err != nil {
				return nil, fmt.Errorf("erreur déploiement proxy C++ (dxgi.dll): %w", err)
			}
			installedFiles = append(installedFiles, "dxgi.dll")
			actions = append(actions, "Installé: Dual-Proxy C++ en tant que dxgi.dll")
		}
	} else {
		// Architecture A : Déploiement standard ReShade
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

	// 3b. Déploiement CUDA Runtime (cudart64_12.dll) si requis
	if plan.CudartPath != "" {
		destCudart := filepath.Join(gameDir, "cudart64_12.dll")
		if dryRun {
			actions = append(actions, "Copier cudart64_12.dll")
		} else {
			if err := copyFile(plan.CudartPath, destCudart); err != nil {
				return nil, fmt.Errorf("erreur déploiement cudart64_12.dll: %w", err)
			}
			installedFiles = append(installedFiles, "cudart64_12.dll")
			actions = append(actions, "Installé: cudart64_12.dll")
		}
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

// ensureLukeRossCompatibility applique l'ensemble des correctifs chirurgicaux REX :
// 1. Neutralise l'export 'ReShadeVersion' -> 'ReShxdeVersion' dans RealVR64.dll (anti-collision)
// 2. Débride la Feature 18 (DLSS 5) dans RealVR64.dll à 0x25EE03 (6x NOP au lieu de rejeter avec BAD0000B)
// 3. Neutralise les hooks dxgi.dll et openvr_api.dll dans ReShade (anti-écran noir / conflit VR)
// 4. Débride le pool de travail de renodx-dlss5.addon64 à 0xDFF5 (pool infini) et 0xA13F (évaluation continue)
func ensureLukeRossCompatibility(gameDir, proxyTarget string) {
	// 1 & 2. Patches sur RealVR64.dll (ou dxgi.dll de LukeRoss avant renommage)
	realVRNames := []string{"RealVR64.dll", "dxgi.dll"}
	for _, name := range realVRNames {
		lrPath := filepath.Join(gameDir, name)
		data, err := os.ReadFile(lrPath)
		if err != nil {
			continue
		}
		modified := false

		// Anti-double injection: ReShadeVersion -> ReShxdeVersion
		needle := []byte("ReShadeVersion\x00")
		if idx := strings.Index(string(data), string(needle)); idx != -1 {
			copy(data[idx:], []byte("ReShxdeVersion\x00"))
			modified = true
		}

		// Feature 18 bypass dans RealVR64.dll à l'offset 0x25EE03 (6x NOP: 0f 85 f1 07 00 00 -> 90 90 90 90 90 90)
		lrOffset := 0x25EE03
		if len(data) > lrOffset+6 {
			expected := []byte{0x0f, 0x85, 0xf1, 0x07, 0x00, 0x00}
			if bytes.Equal(data[lrOffset:lrOffset+6], expected) {
				copy(data[lrOffset:lrOffset+6], []byte{0x90, 0x90, 0x90, 0x90, 0x90, 0x90})
				modified = true
			}
		}

		if modified {
			_ = os.WriteFile(lrPath, data, 0644)
		}
	}

	// 3. Neutraliser les hooks dxgi.dll et openvr_api.dll dans ReShade (anti-écran noir / conflit VR)
	reshadeTargets := []string{proxyTarget, "ReShade64_dlss5.dll"}
	for _, target := range reshadeTargets {
		if target == "" {
			continue
		}
		pPath := filepath.Join(gameDir, target)
		pData, err := os.ReadFile(pPath)
		if err != nil {
			continue
		}
		pModified := false

		openvrWide := []byte("o\x00p\x00e\x00n\x00v\x00r\x00_\x00a\x00p\x00i\x00.\x00d\x00l\x00l\x00")
		openvxWide := []byte("o\x00p\x00e\x00n\x00v\x00x\x00_\x00a\x00p\x00i\x00.\x00d\x00l\x00l\x00")
		if oIdx := strings.Index(string(pData), string(openvrWide)); oIdx != -1 {
			copy(pData[oIdx:], openvxWide)
			pModified = true
		}

		dxgiWide := []byte("d\x00x\x00g\x00i\x00.\x00d\x00l\x00l\x00")
		dxgxWide := []byte("d\x00x\x00g\x00x\x00.\x00d\x00l\x00l\x00")
		if dIdx := strings.Index(string(pData), string(dxgiWide)); dIdx != -1 {
			copy(pData[dIdx:], dxgxWide)
			pModified = true
		}

		if pModified {
			_ = os.WriteFile(pPath, pData, 0644)
		}
	}

	// 4. Débrider renodx-dlss5.addon64 (Pool infini + débridage évaluation continue)
	addonPath := filepath.Join(gameDir, "renodx-dlss5.addon64")
	if aData, err := os.ReadFile(addonPath); err == nil {
		aModified := false

		// Offset 0xDFF5 : c6 42 60 01 -> c6 42 60 00 (pool infini)
		poolOff := 0xDFF5
		if len(aData) > poolOff+4 && bytes.Equal(aData[poolOff:poolOff+4], []byte{0xc6, 0x42, 0x60, 0x01}) {
			aData[poolOff+3] = 0x00
			aModified = true
		}

		// Offset 0xA13F : 74 59 -> 90 90 (débridage évaluation continue)
		evalOff := 0xA13F
		if len(aData) > evalOff+2 && bytes.Equal(aData[evalOff:evalOff+2], []byte{0x74, 0x59}) {
			copy(aData[evalOff:evalOff+2], []byte{0x90, 0x90})
			aModified = true
		}

		// Offset 0xE0DF : 0f 84 bd 00 00 00 -> 90 90 90 90 90 90 (fence wait bypass)
		fenceOff := 0xE0DF
		if len(aData) > fenceOff+6 && bytes.Equal(aData[fenceOff:fenceOff+6], []byte{0x0f, 0x84, 0xbd, 0x00, 0x00, 0x00}) {
			copy(aData[fenceOff:fenceOff+6], []byte{0x90, 0x90, 0x90, 0x90, 0x90, 0x90})
			aModified = true
		}

		// Offset 0xDF91 : 0f 84 82 01 00 00 -> 90 90 90 90 90 90 (exhaustion discard bypass)
		exhOff := 0xDF91
		if len(aData) > exhOff+6 && bytes.Equal(aData[exhOff:exhOff+6], []byte{0x0f, 0x84, 0x82, 0x01, 0x00, 0x00}) {
			copy(aData[exhOff:exhOff+6], []byte{0x90, 0x90, 0x90, 0x90, 0x90, 0x90})
			aModified = true
		}

		// Offset 0xDF64 : 0f 84 8b 00 00 00 -> e9 8c 00 00 00 90 (force recyclage immédiat du slot de travail sans attendre de fence de swapchain absente en VR)
		df64Off := 0xDF64
		if len(aData) > df64Off+6 && bytes.Equal(aData[df64Off:df64Off+6], []byte{0x0f, 0x84, 0x8b, 0x00, 0x00, 0x00}) {
			copy(aData[df64Off:df64Off+6], []byte{0xe9, 0x8c, 0x00, 0x00, 0x00, 0x90})
			aModified = true
		}

		// Offset 0xDF97 : 31 c0 e9 a1 02 00 00 -> e9 59 00 00 00 90 90 (filet de sécurité universel : saut direct vers DFF5, empêche tout retour NULL du pool)
		df97Off := 0xDF97
		if len(aData) > df97Off+7 && bytes.Equal(aData[df97Off:df97Off+7], []byte{0x31, 0xc0, 0xe9, 0xa1, 0x02, 0x00, 0x00}) {
			copy(aData[df97Off:df97Off+7], []byte{0xe9, 0x59, 0x00, 0x00, 0x00, 0x90, 0x90})
			aModified = true
		}

		// Offset 0xA222 : 0f 85 78 01 00 00 -> 90 90 90 90 90 90 (log throttle bypass)
		logOff := 0xA222
		if len(aData) > logOff+6 && bytes.Equal(aData[logOff:logOff+6], []byte{0x0f, 0x85, 0x78, 0x01, 0x00, 0x00}) {
			copy(aData[logOff:logOff+6], []byte{0x90, 0x90, 0x90, 0x90, 0x90, 0x90})
			aModified = true
		}

		// Offsets 0x36EA8 + i*0x4C0 (slots 0 à 7) : 75 56 -> 90 90 (force évaluation continue sur TOUS les 8 slots / ANY_HANDLE)
		for s := 0; s < 8; s++ {
			slotOff := 0x36EA8 + s*0x4C0
			if len(aData) > slotOff+2 && bytes.Equal(aData[slotOff:slotOff+2], []byte{0x75, 0x56}) {
				copy(aData[slotOff:slotOff+2], []byte{0x90, 0x90})
				aModified = true
			}
			// Offset 0x36E9B + s*0x4C0 : 74 63 -> 90 90 (bypass test rdi null check sautant le call evaluate)
			nullOff := 0x36E9B + s*0x4C0
			if len(aData) > nullOff+2 && bytes.Equal(aData[nullOff:nullOff+2], []byte{0x74, 0x63}) {
				copy(aData[nullOff:nullOff+2], []byte{0x90, 0x90})
				aModified = true
			}
			// Offset 0x36F2E + s*0x4C0 : 75 60 -> 90 90 (bypass dl-skip sautant l'envoi de la frame neuronale)
			skipOff := 0x36F2E + s*0x4C0
			if len(aData) > skipOff+2 && bytes.Equal(aData[skipOff:skipOff+2], []byte{0x75, 0x60}) {
				copy(aData[skipOff:skipOff+2], []byte{0x90, 0x90})
				aModified = true
			}
			// Offset 0x36EEF + s*0x4C0 : 74 0F -> 90 90 (force évaluation inconditionnelle pré-DLSS de la Feature 18)
			gate1Off := 0x36EEF + s*0x4C0
			if len(aData) > gate1Off+2 && bytes.Equal(aData[gate1Off:gate1Off+2], []byte{0x74, 0x0f}) {
				copy(aData[gate1Off:gate1Off+2], []byte{0x90, 0x90})
				aModified = true
			}
			// Offset 0x36F70 + s*0x4C0 : 74 0F -> 90 90 (force dispatch inconditionnel post-DLSS de la Feature 18)
			gate2Off := 0x36F70 + s*0x4C0
			if len(aData) > gate2Off+2 && bytes.Equal(aData[gate2Off:gate2Off+2], []byte{0x74, 0x0f}) {
				copy(aData[gate2Off:gate2Off+2], []byte{0x90, 0x90})
				aModified = true
			}
		}

		// Patch des 5 verrous de validation d'état hôte (Host State Incomplete) dans le dispatcher central 0x36FF0 :
		// Offsets 0x36643, 0x36650, 0x3665D, 0x3666A, 0x36677 (chacun 6 octets de conditional jump vers le skip)
		hostGates := []struct {
			offset   int
			expected []byte
		}{
			{0x36643, []byte{0x0F, 0x85, 0xF6, 0x01, 0x00, 0x00}}, // pso_known != 1 -> skip
			{0x36650, []byte{0x0F, 0x84, 0xE9, 0x01, 0x00, 0x00}}, // so_known == 0 -> skip
			{0x3665D, []byte{0x0F, 0x84, 0xDC, 0x01, 0x00, 0x00}}, // root_known == 0 -> skip
			{0x3666A, []byte{0x0F, 0x84, 0xCF, 0x01, 0x00, 0x00}}, // heaps_known == 0 -> skip
			{0x36677, []byte{0x0F, 0x84, 0xC2, 0x01, 0x00, 0x00}}, // graphics_tables_known == 0 -> skip
		}
		nop6 := []byte{0x90, 0x90, 0x90, 0x90, 0x90, 0x90}
		for _, g := range hostGates {
			if len(aData) > g.offset+6 && bytes.Equal(aData[g.offset:g.offset+6], g.expected) {
				copy(aData[g.offset:g.offset+6], nop6)
				aModified = true
			}
		}

		// Patch de la sentinelle de réutilisation de tampon de sortie (Render Target Aliasing Loop Guard) à l'offset 0x7E1D :
		// 0F 84 BE 07 00 00 (je 0x8FE1) -> 90 90 90 90 90 90 (force l'évaluation récurrente sur 100% des frames en VR stéréoscopique)
		rtGuardOff := 0x7E1D
		if len(aData) > rtGuardOff+6 && bytes.Equal(aData[rtGuardOff:rtGuardOff+6], []byte{0x0F, 0x84, 0xBE, 0x07, 0x00, 0x00}) {
			copy(aData[rtGuardOff:rtGuardOff+6], nop6)
			aModified = true
		}

		if aModified {
			_ = os.WriteFile(addonPath, aData, 0644)
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
