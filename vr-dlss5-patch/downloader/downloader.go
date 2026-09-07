package downloader

import (
	"archive/zip"
	"bytes"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"
)

const (
	ReshadeHome     = "https://reshade.me"
	RhiRepoReleases = "https://github.com/RankFTW/rhi-repo/releases"
	RhiRepoRaw      = "https://github.com/RankFTW/rhi-repo"
	FeederRepo      = "jlrouzies-fr/DLSS5-Feeder"
	LumeniteZipUrl  = "https://codeload.github.com/umar-afzaal/LumeniteFX/zip/refs/heads/mainline"
	BridgeUrl       = "https://github.com/NIGos/dlss5-bridge/releases/latest/download/dlss5-bridge.addon64"
	ShadersRaw      = "https://raw.githubusercontent.com/crosire/reshade-shaders/slim/Shaders/"
)

type Downloader struct {
	Client   *http.Client
	CacheDir string
}

func NewDownloader(customCacheDir string) (*Downloader, error) {
	cache := customCacheDir
	if cache == "" {
		localApp := os.Getenv("LOCALAPPDATA")
		if localApp == "" {
			localApp = "."
		}
		cache = filepath.Join(localApp, "vr-dlss5-patch", "cache")
	}
	if err := os.MkdirAll(cache, 0755); err != nil {
		return nil, fmt.Errorf("impossible de créer le dossier cache: %w", err)
	}

	client := &http.Client{
		Timeout: 180 * time.Second,
	}

	return &Downloader{
		Client:   client,
		CacheDir: cache,
	}, nil
}

// GetReShadeDll récupère ReShade64.dll depuis le cache ou téléchargement officiel.
func (d *Downloader) GetReShadeDll(bitness int) (string, error) {
	dllName := "ReShade64.dll"
	if bitness == 32 {
		dllName = "ReShade32.dll"
	}

	cachedDll := filepath.Join(d.CacheDir, dllName)
	if fi, err := os.Stat(cachedDll); err == nil && fi.Size() > 1024*1024 {
		return cachedDll, nil
	}

	// Chercher dans Downloads de l'utilisateur
	if userDownloads := getUserDownloads(); userDownloads != "" {
		matches, _ := filepath.Glob(filepath.Join(userDownloads, "ReShade_Setup_*_Addon.exe"))
		if len(matches) > 0 {
			sort.Strings(matches)
			setupExe := matches[len(matches)-1]
			if err := extractFromZip(setupExe, dllName, cachedDll); err == nil {
				return cachedDll, nil
			}
		}
	}

	// Scraping reshade.me
	fmt.Println("  [ReShade] Recherche de la dernière version avec support Add-on sur reshade.me...")
	body, err := d.fetchText(ReshadeHome)
	if err != nil {
		return "", fmt.Errorf("erreur contact reshade.me: %w", err)
	}

	re := regexp.MustCompile(`/downloads/ReShade_Setup_([\d.]+)_Addon\.exe`)
	m := re.FindStringSubmatch(body)
	if len(m) < 2 {
		return "", fmt.Errorf("lien de téléchargement ReShade Addon introuvable sur reshade.me")
	}

	ver := m[1]
	downloadUrl := ReshadeHome + m[0]
	setupPath := filepath.Join(d.CacheDir, fmt.Sprintf("ReShade_Setup_%s_Addon.exe", ver))

	if _, err := os.Stat(setupPath); os.IsNotExist(err) {
		fmt.Printf("  [ReShade] Téléchargement de ReShade v%s Add-on...\n", ver)
		if err := d.downloadFile(downloadUrl, setupPath); err != nil {
			return "", err
		}
	}

	if err := extractFromZip(setupPath, dllName, cachedDll); err != nil {
		return "", fmt.Errorf("extraction de %s depuis %s: %w", dllName, setupPath, err)
	}

	return cachedDll, nil
}

// GetDLSS5Addon récupère renodx-dlss5.addon64
func (d *Downloader) GetDLSS5Addon() (string, error) {
	cached := filepath.Join(d.CacheDir, "renodx-dlss5.addon64")
	if fi, err := os.Stat(cached); err == nil && fi.Size() > 100*1024 {
		return cached, nil
	}

	// Chercher dans deps/ local ou parent
	depsCandidates := []string{
		filepath.Join("deps", "renodx-dlss5.addon64"),
		filepath.Join("..", "deps", "renodx-dlss5.addon64"),
		`C:\code\vrdlss5\deps\renodx-dlss5.addon64`,
	}
	for _, c := range depsCandidates {
		if fi, err := os.Stat(c); err == nil && fi.Size() > 100*1024 {
			_ = copyFile(c, cached)
			return cached, nil
		}
	}

	// Chercher dans Downloads ou cache
	if userDownloads := getUserDownloads(); userDownloads != "" {
		matches, _ := filepath.Glob(filepath.Join(userDownloads, "*renodx-dlss5*"))
		for _, m := range matches {
			if strings.EqualFold(filepath.Ext(m), ".addon64") {
				_ = copyFile(m, cached)
				return cached, nil
			}
			if strings.EqualFold(filepath.Ext(m), ".zip") {
				if err := extractFromZip(m, "renodx-dlss5.addon64", cached); err == nil {
					return cached, nil
				}
			}
		}
	}

	fmt.Println("  [DLSS 5 Add-on] Recherche de la release renodx-dlss5 sur GitHub rhi-repo...")
	tag, assetUrl, err := d.getRhiRelease("renodx-dlss5-", false)
	if err != nil {
		return "", err
	}

	zipPath := filepath.Join(d.CacheDir, tag+".zip")
	if _, err := os.Stat(zipPath); os.IsNotExist(err) {
		fmt.Printf("  [DLSS 5 Add-on] Téléchargement %s...\n", tag)
		if err := d.downloadFile(assetUrl, zipPath); err != nil {
			return "", err
		}
	}

	if err := extractFromZip(zipPath, "renodx-dlss5.addon64", cached); err != nil {
		return "", err
	}

	return cached, nil
}

// GetDLSSNRModel récupère nvngx_dlssnr.dll (modèle neuronal)
func (d *Downloader) GetDLSSNRModel() (string, error) {
	cached := filepath.Join(d.CacheDir, "nvngx_dlssnr.dll")
	if fi, err := os.Stat(cached); err == nil && fi.Size() > 50*1024*1024 {
		return cached, nil
	}

	// Chercher dans Downloads
	if userDownloads := getUserDownloads(); userDownloads != "" {
		matches, _ := filepath.Glob(filepath.Join(userDownloads, "*dlssnr*"))
		for _, m := range matches {
			if strings.EqualFold(filepath.Base(m), "nvngx_dlssnr.dll") {
				_ = copyFile(m, cached)
				return cached, nil
			}
			if strings.EqualFold(filepath.Ext(m), ".zip") {
				if err := extractFromZip(m, "nvngx_dlssnr.dll", cached); err == nil {
					return cached, nil
				}
			}
		}
	}

	fmt.Println("  [DLSS NR Model] Recherche de la release dlssnr sur GitHub rhi-repo...")
	tag, assetUrl, err := d.getRhiRelease("dlssnr-", true) // préférer .SF si dispo
	if err != nil {
		return "", err
	}

	zipPath := filepath.Join(d.CacheDir, tag+".zip")
	if _, err := os.Stat(zipPath); os.IsNotExist(err) {
		fmt.Printf("  [DLSS NR Model] Téléchargement %s (~110-160 Mo)...\n", tag)
		if err := d.downloadFile(assetUrl, zipPath); err != nil {
			return "", err
		}
	}

	if err := extractFromZip(zipPath, "nvngx_dlssnr.dll", cached); err != nil {
		return "", err
	}

	return cached, nil
}

// GetBridgeAddon récupère dlss5-bridge.addon64 pour les jeux DX11 avec DLSS natif
func (d *Downloader) GetBridgeAddon() (string, error) {
	cached := filepath.Join(d.CacheDir, "dlss5-bridge.addon64")
	if fi, err := os.Stat(cached); err == nil && fi.Size() > 50*1024 {
		return cached, nil
	}
	fmt.Println("  [DX11 Bridge] Téléchargement dlss5-bridge.addon64...")
	if err := d.downloadFile(BridgeUrl, cached); err != nil {
		return "", err
	}
	return cached, nil
}

// Scrape tags from rhi-repo
func (d *Downloader) getRhiRelease(prefix string, preferSF bool) (tag string, downloadUrl string, err error) {
	html, err := d.fetchText(RhiRepoReleases)
	if err != nil {
		return "", "", err
	}

	reTag := regexp.MustCompile(`/releases/tag/(` + regexp.QuoteMeta(prefix) + `[A-Za-z0-9._-]+)`)
	matches := reTag.FindAllStringSubmatch(html, -1)
	if len(matches) == 0 {
		return "", "", fmt.Errorf("aucune release rhi-repo trouvée avec le préfixe %s", prefix)
	}

	var tags []string
	seen := make(map[string]bool)
	for _, m := range matches {
		t := m[1]
		if !seen[t] {
			seen[t] = true
			tags = append(tags, t)
		}
	}

	// Tri par version
	bestTag := pickBestTag(tags, prefix, preferSF)
	if bestTag == "" {
		return "", "", fmt.Errorf("impossible de sélectionner un tag pour %s", prefix)
	}

	// Récupérer l'asset URL depuis expanded_assets
	expandedUrl := fmt.Sprintf("https://github.com/RankFTW/rhi-repo/releases/expanded_assets/%s", bestTag)
	expHtml, err := d.fetchText(expandedUrl)
	if err != nil {
		return "", "", err
	}

	reAsset := regexp.MustCompile(`/releases/download/` + regexp.QuoteMeta(bestTag) + `/([^"]+\.zip)`)
	assetMatches := reAsset.FindStringSubmatch(expHtml)
	if len(assetMatches) < 2 {
		return "", "", fmt.Errorf("aucun asset .zip trouvé pour le tag %s", bestTag)
	}

	return bestTag, fmt.Sprintf("https://github.com/RankFTW/rhi-repo/releases/download/%s/%s", bestTag, assetMatches[1]), nil
}

func pickBestTag(tags []string, prefix string, preferSF bool) string {
	if preferSF {
		var sfTags []string
		for _, t := range tags {
			if strings.Contains(t, ".SF") {
				sfTags = append(sfTags, t)
			}
		}
		if len(sfTags) > 0 {
			tags = sfTags
		}
	}

	type cand struct {
		tag  string
		vers []int
	}
	reNum := regexp.MustCompile(`\d+`)
	var cands []cand
	for _, t := range tags {
		numStrs := reNum.FindAllString(t[len(prefix):], -1)
		var vers []int
		for _, ns := range numStrs {
			n, _ := strconv.Atoi(ns)
			vers = append(vers, n)
		}
		cands = append(cands, cand{tag: t, vers: vers})
	}

	sort.Slice(cands, func(i, j int) bool {
		vi, vj := cands[i].vers, cands[j].vers
		l := len(vi)
		if len(vj) < l {
			l = len(vj)
		}
		for idx := 0; idx < l; idx++ {
			if vi[idx] != vj[idx] {
				return vi[idx] < vj[idx]
			}
		}
		return len(vi) < len(vj)
	})

	if len(cands) == 0 {
		return ""
	}
	return cands[len(cands)-1].tag
}

func (d *Downloader) fetchText(url string) (string, error) {
	req, err := http.NewRequest("GET", url, nil)
	if err != nil {
		return "", err
	}
	req.Header.Set("User-Agent", "vr-dlss5-patch/1.0")

	resp, err := d.Client.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("HTTP %d sur %s", resp.StatusCode, url)
	}

	buf := new(bytes.Buffer)
	_, err = io.Copy(buf, resp.Body)
	return buf.String(), err
}

func (d *Downloader) downloadFile(url string, destPath string) error {
	tmpPath := destPath + ".tmp"
	req, err := http.NewRequest("GET", url, nil)
	if err != nil {
		return err
	}
	req.Header.Set("User-Agent", "vr-dlss5-patch/1.0")

	resp, err := d.Client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("HTTP %d lors du téléchargement de %s", resp.StatusCode, url)
	}

	out, err := os.Create(tmpPath)
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, resp.Body)
	if err != nil {
		_ = os.Remove(tmpPath)
		return err
	}
	out.Close()

	return os.Rename(tmpPath, destPath)
}

func extractFromZip(zipPath string, targetFileName string, destPath string) error {
	r, err := zip.OpenReader(zipPath)
	if err != nil {
		return err
	}
	defer r.Close()

	for _, f := range r.File {
		base := filepath.Base(f.Name)
		if strings.EqualFold(base, targetFileName) {
			rc, err := f.Open()
			if err != nil {
				return err
			}
			defer rc.Close()

			_ = os.MkdirAll(filepath.Dir(destPath), 0755)
			out, err := os.Create(destPath)
			if err != nil {
				return err
			}
			defer out.Close()

			_, err = io.Copy(out, rc)
			return err
		}
	}
	return fmt.Errorf("fichier %s introuvable dans %s", targetFileName, zipPath)
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

func getUserDownloads() string {
	userProfile := os.Getenv("USERPROFILE")
	if userProfile == "" {
		return ""
	}
	dl := filepath.Join(userProfile, "Downloads")
	if fi, err := os.Stat(dl); err == nil && fi.IsDir() {
		return dl
	}
	return ""
}
