<?php
// mycelius system update server
// Endpoint: https://mikxxie.k.vu/__private/mycupdates.php
// Usage: GET ?version=1
// Returns: .msu binary update package or HTTP 204 No Content

header('Content-Type: application/octet-stream');
header('Cache-Control: no-cache, no-store, must-revalidate');
header('Pragma: no-cache');

$update_dir = __DIR__ . '/updates';
$version_file = $update_dir . '/version.json';
$current_version = 1;

if (!is_dir($update_dir)) {
    mkdir($update_dir, 0755, true);
}

// Read current version from JSON
if (file_exists($version_file)) {
    $data = json_decode(file_get_contents($version_file), true);
    if (is_array($data) && isset($data['version'])) {
        $current_version = (int)$data['version'];
    }
}

// Get client version from query string
$client_version = isset($_GET['version']) ? (int)$_GET['version'] : 0;

// If client is up to date, return 204 No Content
if ($client_version >= $current_version) {
    http_response_code(204);
    exit;
}

// Find the latest .msu update package
$msu_files = glob($update_dir . '/*.msu');
$update_file = null;
$update_version = 0;

foreach ($msu_files as $f) {
    $basename = basename($f, '.msu');
    // Parse version from filename: backboardd-2.msu -> version 2
    if (preg_match('/-(\d+)$/', $basename, $m)) {
        $v = (int)$m[1];
        if ($v > $update_version && $v > $client_version) {
            $update_version = $v;
            $update_file = $f;
        }
    }
}

if ($update_file === null || !file_exists($update_file)) {
    http_response_code(204);
    exit;
}

// Serve the .msu update package
header('Content-Type: application/octet-stream');
header('Content-Disposition: attachment; filename="' . basename($update_file) . '"');
header('Content-Length: ' . filesize($update_file));
header('X-Mycelius-Version: ' . $update_version);
readfile($update_file);
exit;
