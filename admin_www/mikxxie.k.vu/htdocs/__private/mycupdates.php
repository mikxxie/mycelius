<?php
// mycelius system update server
// Endpoint: https://mikxxie.k.vu/__private/mycupdates.php
//
// Usage:
//   GET  ?version=N          - Get latest .msu if client version < server version (204 if up to date)
//   GET  ?list=1             - JSON list of all available updates
//   POST ?file=name.msu      - Upload an .msu update package (multipart/form-data)
//
// .msu naming: <daemon>-<version>.msu  (e.g. backboardd-2.msu, launchpad-3.msu)
// version.json tracks: {"version": N, "updates": {"backboardd": 2, "launchpad": 3}}

declare(strict_types=1);

header('Cache-Control: no-cache, no-store, must-revalidate');
header('Pragma: no-cache');

$update_dir = __DIR__ . '/updates';
$version_file = $update_dir . '/version.json';

if (!is_dir($update_dir)) {
    @mkdir($update_dir, 0755, true);
}

// Read version.json
$current_version = 0;
$daemon_versions = [];
if (file_exists($version_file)) {
    $data = json_decode(file_get_contents($version_file), true);
    if (is_array($data)) {
        $current_version = (int)($data['version'] ?? 0);
        $daemon_versions = is_array($data['updates'] ?? []) ? $data['updates'] : [];
    }
}

// Client version from query
$client_version = isset($_GET['version']) ? (int)$_GET['version'] : 0;

// ---------- JSON LIST ENDPOINT ----------
if (isset($_GET['list']) && $_GET['list'] === '1') {
    header('Content-Type: application/json');
    echo json_encode([
        'server_version' => $current_version,
        'daemons' => $daemon_versions,
        'updates_available' => [],
    ]);
    exit;
}

// ---------- UPLOAD ENDPOINT ----------
if ($_SERVER['REQUEST_METHOD'] === 'POST' && isset($_FILES['file'])) {
    $uploaded = $_FILES['file'];
    if ($uploaded['error'] === UPLOAD_ERR_OK) {
        $name = basename($uploaded['name']);
        // Validate .msu extension
        if (preg_match('/^[a-zA-Z0-9_-]+-\d+\.msu$/', $name)) {
            $dest = $update_dir . '/' . $name;
            if (move_uploaded_file($uploaded['tmp_name'], $dest)) {
                // Update version.json
                if (preg_match('/-(\d+)\.msu$/', $name, $mv)) {
                    $new_ver = (int)$mv[1];
                    $daemon_name = substr($basename, 0, -(strlen($mv[0])));
                    $daemon_versions[$daemon_name] = $new_ver;
                    if ($new_ver > $current_version) {
                        $current_version = $new_ver;
                    }
                    file_put_contents($version_file, json_encode([
                        'version' => $current_version,
                        'updates' => $daemon_versions,
                    ], JSON_PRETTY_PRINT));
                }
                http_response_code(200);
                echo json_encode(['status' => 'ok', 'file' => $name, 'version' => $new_ver ?? 0]);
                exit;
            }
        }
    }
    http_response_code(400);
    echo json_encode(['status' => 'error', 'message' => 'Invalid or failed upload']);
    exit;
}

// ---------- DOWNLOAD ENDPOINT (GET ?version=N) ----------
header('Content-Type: application/octet-stream');

if ($client_version >= $current_version) {
    http_response_code(204);
    exit;
}

// Find the best matching .msu (highest version > client_version)
$msu_files = glob($update_dir . '/*.msu');
$update_file = null;
$update_version = 0;

foreach ($msu_files as $f) {
    $basename = basename($f, '.msu');
    if (preg_match('/-(\d+)\.msu$/', $basename, $m)) {
        $v = (int)$m[1];
        if ($v > $update_version && $v > $client_version) {
            $update_version = $v;
            $update_file = $f;
        }
    }
}

if ($update_file === null || !file_exists($update_file) || !is_readable($update_file)) {
    http_response_code(204);
    exit;
}

header('Content-Disposition: attachment; filename="' . basename($update_file) . '"');
header('Content-Length: ' . filesize($update_file));
header('X-Mycelius-Version: ' . $update_version);
readfile($update_file);
exit;
