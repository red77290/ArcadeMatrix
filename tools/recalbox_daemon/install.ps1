<#
.SYNOPSIS
    ArcadeMatrix Recalbox/Batocera daemon installer - run this on your Windows PC, NOT on the
    Recalbox/Batocera device itself.

.DESCRIPTION
    Connects over SSH (using Windows 10/11's built-in OpenSSH client - ssh.exe/scp.exe, present by
    default since Windows 10 1809), auto-detects whether the target is Recalbox or Batocera,
    uploads the right daemon/hook script (with your ArcadeMatrix device's IP baked in), and reboots
    the target so it starts sending game events over MQTT.

    Mirrors install.sh (macOS/Linux) and ArcadeMatrix_RPi's core/ssh_installer.py, for users who
    don't want to (or can't) use WSL/Git Bash.

.NOTES
    Requires ssh.exe and scp.exe to be on PATH. Check with: Get-Command ssh
    If missing: Settings > Apps > Optional Features > Add a feature > OpenSSH Client.

    Unlike install.sh, this script does not attempt to auto-supply the SSH password (Windows'
    OpenSSH client has no built-in "sshpass" equivalent without extra tooling) - you will be
    prompted for the password by ssh/scp themselves (recalboxroot for Recalbox, linux for
    Batocera).
#>

$ErrorActionPreference = "Stop"

Write-Host "=============================================="
Write-Host " ArcadeMatrix Recalbox/Batocera Daemon Installer"
Write-Host "=============================================="
Write-Host ""

if (-not (Get-Command ssh -ErrorAction SilentlyContinue)) {
    Write-Error "ssh.exe not found on PATH. Install the Windows OpenSSH Client: Settings > Apps > Optional Features > Add a feature > OpenSSH Client."
    exit 1
}

$TargetIp = Read-Host "IP address of your Recalbox/Batocera device"
$Action = Read-Host "Action (1: Install Daemon, 2: Check Logs) [1]"
if ([string]::IsNullOrWhiteSpace($Action)) { $Action = "1" }

if ($Action -eq "1") {
    $BrokerIp = Read-Host "IP address of your ArcadeMatrix device (ESP32 or Raspberry Pi)"
    if ([string]::IsNullOrWhiteSpace($BrokerIp)) {
        Write-Error "Broker IP address is required for installation. Aborting."
        exit 1
    }
}

if ([string]::IsNullOrWhiteSpace($TargetIp)) {
    Write-Error "Target IP address is required. Aborting."
    exit 1
}

$CustomUser = Read-Host "Custom SSH Username (leave blank for 'root')"
if ([string]::IsNullOrWhiteSpace($CustomUser)) { $CustomUser = "root" }

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SshOpts = @("-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=NUL", "-o", "ConnectTimeout=5")

function Invoke-RemoteCommand {
    # Suppress the remote command's own stdout from PowerShell's success-output pipeline (it would
    # otherwise get concatenated with $LASTEXITCODE into a single array return value, since any
    # unredirected external-command output becomes part of the function's return). We only care
    # about the exit code here; ssh's interactive password prompt still goes to the real console
    # (TTY), unaffected by this stdout redirect.
    param([string]$Command)
    & ssh @SshOpts "${CustomUser}@${TargetIp}" $Command | Out-Null
    return $LASTEXITCODE
}

function Copy-ToRemote {
    param([string]$LocalPath, [string]$RemotePath)
    & scp @SshOpts $LocalPath "${CustomUser}@${TargetIp}:${RemotePath}"
    if ($LASTEXITCODE -ne 0) { throw "scp failed uploading $LocalPath" }
}

Write-Host "Connecting to $TargetIp - you may be prompted for the SSH password"
Write-Host "(try 'recalboxroot' for Recalbox, 'linux' for Batocera)."
Write-Host ""

$system = "unknown"
if ((Invoke-RemoteCommand "test -d /recalbox/share") -eq 0) {
    $system = "recalbox"
} elseif ((Invoke-RemoteCommand "test -d /userdata/system") -eq 0) {
    $system = "batocera"
}

if ($system -eq "unknown") {
    Write-Error "Could not detect Recalbox or Batocera on $TargetIp. Check the IP, that SSH is enabled, and the password you entered."
    exit 1
}

Write-Host "Detected: $system"

if ($Action -eq "2") {
    $LogPath = if ($system -eq "recalbox") { "/recalbox/share/userscripts/daemon.log" } else { "/userdata/system/scripts/daemon.log" }
    Write-Host ""
    Write-Host "=============================================="
    Write-Host " Fetching logs from $LogPath..."
    Write-Host "=============================================="
    & ssh @SshOpts "${CustomUser}@${TargetIp}" "tail -n 100 $LogPath || echo 'Log file not found or empty'"
    exit 0
}

$TmpDir = Join-Path $env:TEMP "arcadematrix_installer_$(Get-Random)"
New-Item -ItemType Directory -Path $TmpDir | Out-Null

try {
    if ($system -eq "recalbox") {
        $TargetDir = "/recalbox/share/userscripts"
        $daemonSrc = Get-Content (Join-Path $ScriptDir "arcadematrix_daemon.py") -Raw
        $daemonSrc = $daemonSrc.Replace("{{BROKER}}", $BrokerIp)
        $daemonLocal = Join-Path $TmpDir "arcadematrix_daemon.py"
        # ESP32/Recalbox side expects LF line endings; force them explicitly since PowerShell's
        # Set-Content defaults to CRLF on Windows, which would otherwise corrupt the Python file.
        [System.IO.File]::WriteAllText($daemonLocal, $daemonSrc.Replace("`r`n", "`n"))

        Write-Host "Cleaning up any previous install..."
        Invoke-RemoteCommand "pkill -f arcadematrix_daemon.py || true; pkill -f arcadematrix_mqtt.sh || true; rm -f $TargetDir/arcadematrix_mqtt.sh" | Out-Null
        Invoke-RemoteCommand "mkdir -p $TargetDir" | Out-Null

        Write-Host "Uploading daemon..."
        Copy-ToRemote $daemonLocal "/recalbox/share/arcadematrix_daemon.py"
        
        $launcherSrc = Get-Content (Join-Path $ScriptDir "arcadematrix_launcher(permanent).sh") -Raw
        $launcherLocal = Join-Path $TmpDir "arcadematrix_launcher(permanent).sh"
        [System.IO.File]::WriteAllText($launcherLocal, $launcherSrc.Replace("`r`n", "`n"))
        Copy-ToRemote $launcherLocal "$TargetDir/arcadematrix_launcher(permanent).sh"
        
        Invoke-RemoteCommand "chmod +x '$TargetDir/arcadematrix_launcher(permanent).sh'" | Out-Null
    } else {
        $TargetDir = "/userdata/system/scripts"
        $hookSrc = Get-Content (Join-Path $ScriptDir "arcadematrix_mqtt_batocera.sh") -Raw
        $hookSrc = $hookSrc.Replace("{{BROKER}}", $BrokerIp)
        $hookLocal = Join-Path $TmpDir "arcadematrix_mqtt.sh"
        [System.IO.File]::WriteAllText($hookLocal, $hookSrc.Replace("`r`n", "`n"))

        Write-Host "Cleaning up any previous install..."
        Invoke-RemoteCommand "pkill -f arcadematrix_mqtt.sh || true" | Out-Null
        Invoke-RemoteCommand "mkdir -p $TargetDir" | Out-Null

        Write-Host "Uploading hook script..."
        Copy-ToRemote $hookLocal "$TargetDir/arcadematrix_mqtt.sh"
        Invoke-RemoteCommand "chmod +x $TargetDir/arcadematrix_mqtt.sh" | Out-Null
    }

    Write-Host "Rebooting $TargetIp to apply changes..."
    Invoke-RemoteCommand "sleep 1 && reboot" | Out-Null

    Write-Host ""
    Write-Host "=============================================="
    Write-Host " Done! $system is rebooting."
    Write-Host " It will publish game events to MQTT broker ${BrokerIp}:1883 on topic"
    Write-Host " recalbox/system/playing once it's back up."
    Write-Host "=============================================="
} finally {
    Remove-Item -Recurse -Force $TmpDir -ErrorAction SilentlyContinue
}
