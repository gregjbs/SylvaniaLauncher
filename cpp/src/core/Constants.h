#pragma once

#include <QStringList>

// Centralised tunable values previously hardcoded across the UI / managers.
// Add new constants here rather than scattering literals through the codebase.

namespace SylvaniaConstants {

// --- Main window --------------------------------------------------------
// Default size on first launch / after "reset window size"; the window is now
// resizable, so these are no longer a hard cap.
constexpr int kMainWindowWidth = 920;
constexpr int kMainWindowHeight = 580;
// Smallest size at which the layout stays usable.
constexpr int kMainWindowMinWidth = 820;
constexpr int kMainWindowMinHeight = 540;
constexpr int kLogoSizePx = 140;

// --- Spacing scale (px) ---------------------------------------------------
// Single source of truth for layout margins/gutters: every layout margin or
// spacing in the UI uses one of these steps (base 8), never a magic number.
constexpr int kSpaceXs = 4;
constexpr int kSpaceSm = 8;
constexpr int kSpaceMd = 16;
constexpr int kSpaceLg = 24;

// --- Button system (shared gabarits, see ui/ButtonStyles) -----------------
constexpr int kButtonHeightPrimary = 48;    // one per screen (Jouer/Installer)
constexpr int kButtonHeightSecondary = 36;  // nav + common actions
constexpr int kButtonHeightTertiary = 28;   // small toggles / icon buttons
constexpr int kButtonRadiusPx = 8;

// --- Timers (milliseconds) ---------------------------------------------
constexpr int kPlayTimeCheckMs = 5000;        // MainWindow: poll Wow.exe status
constexpr int kExtractProgressMs = 5000;      // HdPatchManager: extraction progress
constexpr int kConfigSaveDebounceMs = 200;    // ConfigManager: coalesce setter writes
constexpr int kHashVerifyDelayMs = 500;       // HdPatchManager: handle release delay
constexpr int kReminderCheckMs = 30000;       // ReminderService: due-reminder poll

// --- Limits -------------------------------------------------------------
constexpr long long kMaxImportBytes = 10LL * 1024 * 1024;  // notes import cap (10 MB)
// Free disk space required before downloading a client, as a multiple of the
// archive size (archive + extracted tree coexist during installation).
constexpr double kClientDiskSpaceFactor = 2.5;

// --- Segmented download (large clients) ---------------------------------
// Multi-GB clients are fetched in HTTP Range segments rather than one giant
// streamed response: each segment is an ordinary GET, the download is
// resumable (offset = bytes already on disk), and a flaky segment is retried
// without restarting from zero. Only used when the total size is known AND the
// server supports byte ranges (206); otherwise a single stream is used.
constexpr long long kDownloadSegmentBytes = 32LL * 1024 * 1024;  // 32 MiB
constexpr int kDownloadSegmentRetries = 5;     // consecutive no-progress retries

// --- Errai -----------------------------------------------------------------
// Serveur par défaut de cette version du launcher (fork Errai). Nouvelle
// installation : seule entrée de la liste. Installation existante : ajouté en
// tête et sélectionné une fois (ConfigManager::migrateErraiDefault).
inline constexpr const char* kErraiRealmName = "Errai";
inline constexpr const char* kErraiHost = "errai.minotux.com";
inline constexpr const char* kErraiRealmlist = "set realmlist errai.minotux.com";
// Client, patch HD et pack anglais sont servis par le portail d'Errai. client.json
// (même dossier) donne l'adresse, la taille et le SHA-256 de chaque archive ; les
// adresses ci-dessous ne servent que si ce manifeste est injoignable.
inline constexpr const char* kErraiManifestUrl =
    "https://errai.minotux.com/telechargements/client/client.json";

// --- Network ------------------------------------------------------------
inline constexpr const char* kServerHost = "sylvania-servergame.com";
inline constexpr const char* kHdPatchUrl =
    "https://errai.minotux.com/telechargements/client/Patch-HD.zip";
inline constexpr const char* kAddonDownloadEndpoint =
    "https://sylvania-servergame.com/download_addon.php";
// Remote manifest describing the "recommended addons" (id, version, download
// URL, expected folders). Editing it server-side adds an addon without a client
// rebuild; if it cannot be fetched the launcher falls back to the copy embedded
// in resources (:/addons/manifest).
inline constexpr const char* kAddonManifestUrl =
    "https://sylvania-servergame.com/launcher/addons.json";

// --- WotLK 3.3.5a client -------------------------------------------------
inline constexpr const char* kWotlkClientUrl =
    "https://errai.minotux.com/telechargements/client/WoW-3.3.5.zip";
inline constexpr const char* kEnUsPackUrl =
    "https://errai.minotux.com/telechargements/client/enUS.zip";
// Integrity data not published by the endpoint yet: 0/empty = "unknown".
// Filling these in makes the download size+hash enforced automatically.
inline constexpr long long kWotlkClientExpectedSize = 0;
inline constexpr const char* kWotlkClientSha256 = "";

// --- Legion 7.3.5 client --------------------------------------------------
inline constexpr const char* kLegionClientUrl =
    "https://legendesylvania.com/downloads/Legion7.3.5.tar.gz";
// Official integrity data (server-side sha256sum + stat, confirmed against the
// HTTP Content-Length). The download is verified size + SHA-256 BEFORE any
// extraction; a mismatch aborts and deletes the file.
inline constexpr long long kLegionClientExpectedSize = 47702669464LL;
inline constexpr const char* kLegionClientSha256 =
    "5d384ac788f985b7d767723c9b228378ee521d8d50500a5d5ee4864deed9af24";
// Legion auth server. On Legion 7.3.5 realmlist.wtf is ignored; the client
// connects via the SET portal line of WTF/Config.wtf (port 3724 is appended
// automatically by the client). Written/patched before launch.
inline constexpr const char* kLegionDefaultPortal = "164.132.43.2";
// The placeholder portal that shipped in the very first v3.0 build, before the
// real auth server was known. ConfigManager rewrites Legion entries still
// carrying it to kLegionDefaultPortal (one-time, Legion-scoped) so early
// configs pick up the correct value.
inline constexpr const char* kLegionPlaceholderPortal = "sylvania-servergame.com";

// --- CASC CDN mirror (Legion 7.3.5) -------------------------------------
// Blizzard's 7.3.5 CDN is dead (404), so the client can't stream missing
// files. Before launch we repoint the "CDN Hosts"/"CDN Servers" fields of the
// client's .build.info to our mirror (reverse-proxy to the wow.tools archive).
// Change these two values if the mirror moves.
inline constexpr const char* kCdnMirrorHost = "legendesylvania.com";
inline constexpr const char* kCdnMirrorServers = "http://legendesylvania.com/";
// Legion addon manifest (own list — never mixed with WotLK's). The recommended
// addons live on legendesylvania.com; until a JSON manifest is published at
// this URL the launcher falls back to the embedded copy (:/addons/manifest_legion).
inline constexpr const char* kLegionAddonManifestUrl =
    "https://legendesylvania.com/addons.json";
// Host serving the Legion addon archives (validated before any download).
inline constexpr const char* kLegionAddonHost = "legendesylvania.com";

// --- Archive extraction -------------------------------------------------
// Arguments passed to "powershell" to extract a ZIP.
//
// Expand-Archive is the PRIMARY method: it reads the ZIP central directory
// (.NET ZipArchive) and so correctly handles archives with data descriptors
// / ZIP64 that servers commonly produce. bsdtar (tar.exe) is much faster but
// aborts with "ZIP bad CRC" on exactly those archives, so it is only used as
// a FALLBACK when Expand-Archive fails (e.g. a path exceeding the 260-char
// MAX_PATH limit, where tar copes better).
//
// We deliberately do NOT set $ErrorActionPreference='Stop' globally: under
// Stop, a native command writing to stderr (tar) is turned into a fatal
// NativeCommandError, which previously aborted the script before the fallback
// could run. Expand-Archive uses a local -ErrorAction Stop inside try/catch
// instead, and tar's success is checked via $LASTEXITCODE.
//
// Paths flow through the SYL_SRC / SYL_DST environment variables (set on the
// QProcess), never interpolated into the command -> no injection surface.
inline QStringList extractArchiveArgs() {
    return QStringList()
        << "-NoProfile"
        << "-ExecutionPolicy" << "Bypass"
        << "-Command"
        << "$src=$env:SYL_SRC; $dst=$env:SYL_DST;"
           "if (-not (Test-Path -LiteralPath $dst)) {"
           " New-Item -ItemType Directory -Force -Path $dst | Out-Null };"
           "$ok=$false;"
           "if (Get-Command tar.exe -ErrorAction SilentlyContinue) {"
           " & tar.exe -xf $src -C $dst 2>$null;"
           " if ($LASTEXITCODE -eq 0) { $ok=$true } };"
           "if (-not $ok) {"
           " try { Expand-Archive -LiteralPath $src -DestinationPath $dst -Force -ErrorAction Stop;"
           " $ok=$true } catch { $ok=$false } };"
           "if ($ok) { exit 0 } else { exit 1 }";
}

// --- tar.gz extraction (Legion client) -----------------------------------
// Expand-Archive cannot read .tar.gz, so the Legion client uses tar.exe
// (bsdtar, shipped with Windows 10 >= 1803 / 11). Same security principle as
// the ZIP path: SYL_SRC / SYL_DST flow through QProcess environment variables
// and are never interpolated into the command string.

// Extracts gzip+tar into the destination directory.
//
// Anti path-traversal: bsdtar (libarchive) is SECURE BY DEFAULT. Without the
// -P / --absolute-paths flag it strips any leading "/" and refuses entries
// that use ".." to escape the destination — so a single extraction pass is
// safe. We deliberately do NOT pre-list the archive (tar -tzf) first: on a
// multi-GB client that means fully decompressing the whole stream twice
// (once to list, once to extract), which is slow enough to look like a hang.
//
// tar's stderr is intentionally NOT redirected to $null so a real failure
// (disk space, unreadable entry, long path) is captured by the QProcess and
// surfaced to the user instead of a blank error.
//
// --strip-components=1 drops the leading "./" component every entry carries
// (the client archive is built with `tar czf x.tar.gz .`, so every path is
// "./Wow.exe", "./Data/...", plus a bare "./" self-entry). Dropping it is a
// no-op for the file layout — "./Wow.exe" and "Wow.exe" resolve to the same
// place — but it removes the "./" self-entry, which is what broke extraction
// to a drive root: bsdtar uses extended-length "\\?\" paths where "." is a
// LITERAL name, so the self-entry became "\\?\G:\." and Windows rejected it
// ("Can't create '\\?\G:\.': Permission denied"). All real files extracted,
// but tar then exited non-zero and the whole install was reported as failed.
inline QStringList extractTarGzArgs() {
    return QStringList()
        << "-NoProfile"
        << "-ExecutionPolicy" << "Bypass"
        << "-Command"
        << "$src=$env:SYL_SRC; $dst=$env:SYL_DST;"
           "if (-not (Test-Path -LiteralPath $dst)) {"
           " New-Item -ItemType Directory -Force -Path $dst | Out-Null };"
           "& tar.exe -xzf $src --strip-components=1 -C $dst;"
           "exit $LASTEXITCODE";
}

}  // namespace SylvaniaConstants
