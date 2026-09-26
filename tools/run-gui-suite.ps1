param(
    [ValidateSet("smoke", "core", "extended", "slow")]
    [string]$Group = "smoke",
    [string]$ScenarioId = "",
    [string]$ManifestPath = "",
    [string]$Executable = "",
    [string]$HeadlessExecutable = "",
    [switch]$ValidateOnly,
    [switch]$SelfTest,
    [string]$ReviewSummary = "",
    [ValidateSet("pass", "fail")]
    [string]$ReviewResult = "pass",
    [string]$ReviewNote = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $RepoRoot "gui-scripts/manifest.json"
}
if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $RepoRoot "build/gui/inop.exe"
}
if ([string]::IsNullOrWhiteSpace($HeadlessExecutable)) {
    $HeadlessExecutable = Join-Path $RepoRoot "build/gui/inop_gui_headless_tests.exe"
}

function Get-ScriptFacts {
    param([string]$Path)
    $shots = [System.Collections.Generic.List[string]]::new()
    $waitSeconds = 0.0
    $lineNumber = 0
    $numberStyle = [System.Globalization.NumberStyles]::Float
    $numberCulture = [System.Globalization.CultureInfo]::InvariantCulture
    foreach ($raw in Get-Content -LiteralPath $Path) {
        $lineNumber++
        $line = $raw.Trim()
        if ($line.Length -eq 0 -or $line.StartsWith("#")) { continue }
        $parts = $line -split "\s+"
        $verb = $parts[0]
        $tail = if ($line.Length -gt $verb.Length) { $line.Substring($verb.Length).Trim() } else { "" }
        $fail = { param([string]$Reason) throw "$Path`:$lineNumber`: $Reason" }
        switch ($verb) {
            "move" {
                if ($parts.Count -ne 3) { & $fail "move needs exactly two coordinates" }
                $value = 0.0
                if (-not [double]::TryParse($parts[1], $numberStyle, $numberCulture, [ref]$value) -or -not [double]::TryParse($parts[2], $numberStyle, $numberCulture, [ref]$value)) { & $fail "move coordinates must be numbers" }
            }
            "resize" {
                if ($parts.Count -ne 3) { & $fail "resize needs exactly two dimensions" }
                $width = 0
                $height = 0
                if (-not [int]::TryParse($parts[1], [ref]$width) -or -not [int]::TryParse($parts[2], [ref]$height) -or $width -lt 320 -or $height -lt 240) { & $fail "resize dimensions must be integers of at least 320 by 240" }
            }
            { $_ -in @("click", "press", "release", "quit") } {
                if ($parts.Count -ne 1) { & $fail "$verb takes no value" }
            }
            "type" {
                if ($tail.Length -eq 0) { & $fail "type needs text" }
            }
            "key" {
                if ($parts.Count -ne 2 -or $parts[1] -notmatch "^(enter|escape|backspace|delete|tab|up|down|left|right|[A-Za-z])$") { & $fail "key value is not supported" }
            }
            { $_ -in @("ctrl", "shift", "alt") } {
                if ($parts.Count -ne 2 -or $parts[1] -notin @("on", "off")) { & $fail "$verb needs on or off" }
            }
            "scroll" {
                $value = 0.0
                if ($parts.Count -ne 2 -or -not [double]::TryParse($parts[1], $numberStyle, $numberCulture, [ref]$value)) { & $fail "scroll needs one number" }
            }
            "wait" {
                $value = 0.0
                if ($parts.Count -ne 2 -or -not [double]::TryParse($parts[1], $numberStyle, $numberCulture, [ref]$value) -or $value -lt 0) { & $fail "wait needs one nonnegative number" }
                $waitSeconds += $value
            }
            "shot" {
                if ($tail.Length -eq 0 -or $tail.Contains("/") -or $tail.Contains("\") -or $tail -in @(".", "..")) { & $fail "shot needs one safe file name" }
                $shots.Add($tail)
            }
            default { & $fail "unknown verb $verb" }
        }
    }
    [pscustomobject]@{ Shots = @($shots); WaitSeconds = $waitSeconds }
}

function Test-Manifest {
    param($Manifest)
    if ($Manifest.format_version -ne 1) { throw "manifest format_version must be 1" }
    $knownGroups = @($Manifest.groups.PSObject.Properties.Name)
    foreach ($required in @("smoke", "core", "extended", "slow")) {
        if ($required -notin $knownGroups) { throw "manifest group is missing: $required" }
    }
    $knownProfiles = @($Manifest.isolation_profiles.PSObject.Properties.Name)
    $ids = @($Manifest.scenarios | ForEach-Object { $_.id })
    $paths = @($Manifest.scenarios | ForEach-Object { $_.script })
    if (($ids | Sort-Object -Unique).Count -ne $ids.Count) { throw "manifest has a duplicate scenario id" }
    if (($paths | Sort-Object -Unique).Count -ne $paths.Count) { throw "manifest has a duplicate script path" }
    $diskPaths = @(Get-ChildItem -LiteralPath (Join-Path $RepoRoot "gui-scripts") -Filter "*.txt" -File | ForEach-Object { "gui-scripts/$($_.Name)" } | Sort-Object)
    $listedPaths = @($paths | Sort-Object)
    if (Compare-Object -ReferenceObject $diskPaths -DifferenceObject $listedPaths) {
        $difference = Compare-Object -ReferenceObject $diskPaths -DifferenceObject $listedPaths | ForEach-Object { "$($_.SideIndicator) $($_.InputObject)" }
        throw "manifest inventory does not match scripts: $($difference -join "; ")"
    }
    foreach ($scenario in $Manifest.scenarios) {
        if ([string]::IsNullOrWhiteSpace($scenario.subsystem) -or [string]::IsNullOrWhiteSpace($scenario.behavior) -or [string]::IsNullOrWhiteSpace($scenario.expected_outcome)) { throw "scenario metadata is incomplete: $($scenario.id)" }
        if (@($scenario.preconditions).Count -eq 0 -or @($scenario.actions).Count -eq 0) { throw "scenario inventory detail is incomplete: $($scenario.id)" }
        if ($scenario.isolation -notin $knownProfiles) { throw "unknown isolation profile for $($scenario.id): $($scenario.isolation)" }
        foreach ($name in @($scenario.groups)) {
            if ($name -notin $knownGroups) { throw "unknown group for $($scenario.id): $name" }
        }
        if ($scenario.timeout_seconds -le 0 -or $scenario.approximate_duration_seconds -lt 0) { throw "invalid timing for $($scenario.id)" }
        if ($scenario.expected_exit -lt 0) { throw "invalid expected exit for $($scenario.id)" }
        if ($scenario.review_mode -notin @("automatic", "visual", "both")) { throw "invalid review mode for $($scenario.id)" }
        $scriptPath = Join-Path $RepoRoot $scenario.script
        $facts = Get-ScriptFacts -Path $scriptPath
        $declared = @($scenario.screenshots)
        if (Compare-Object -ReferenceObject @($facts.Shots) -DifferenceObject $declared -SyncWindow 0) { throw "screenshot declaration does not match script order: $($scenario.id)" }
        if ($scenario.approximate_duration_seconds -lt $facts.WaitSeconds) { throw "approximate duration is below declared waits: $($scenario.id)" }
    }
}

function Write-IsolationProfile {
    param($Profile, [string]$Directory)
    New-Item -ItemType Directory -Path $Directory -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $Directory "setup") -Force | Out-Null
    if ($Profile.preferences -ne "none") {
        $prefs = [ordered]@{
            theme = "system"
            colourblind = "full"
            window_mode = "windowed"
            vsync = $true
            frame_rate_limit = 0
            font = "CrimsonPro.ttf"
            zoom = 110
            reduced_motion = $false
            audio = [ordered]@{ muted = $true; volume = 0 }
            tutorial = [ordered]@{ done = $true; section = 0; launches = 4 }
        }
        if ($Profile.preferences -eq "wide-font") { $prefs.font = "sga-all-characters.otf" }
        if ($Profile.preferences -eq "frame-rate-180") { $prefs.vsync = $false; $prefs.frame_rate_limit = 180 }
        if ($Profile.preferences -eq "reduced-motion") { $prefs.reduced_motion = $true }
        $prefs | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Directory "inop.gui.json") -Encoding utf8NoBOM
    }
    foreach ($fixture in @($Profile.fixtures)) {
        if ($fixture -eq "application-licence") {
            Copy-Item -LiteralPath (Join-Path $RepoRoot "LICENSE") -Destination (Join-Path $Directory "LICENSE")
        }
        if ($fixture -eq "malformed-preset") {
            Set-Content -LiteralPath (Join-Path $Directory "setup/gui-suite-invalid.inop") -Value "{ invalid preset" -Encoding utf8NoBOM
        }
    }
}

function Invoke-CapturedProcess {
    param([string]$FilePath, [string[]]$Arguments, [string]$WorkingDirectory, [int]$TimeoutSeconds)
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $FilePath
    $info.WorkingDirectory = $WorkingDirectory
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $info
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    if (-not $process.Start()) { throw "could not start $FilePath" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $finished = $process.WaitForExit($TimeoutSeconds * 1000)
    if (-not $finished) {
        $process.Kill($true)
        $process.WaitForExit()
    }
    $clock.Stop()
    [pscustomobject]@{
        TimedOut = -not $finished
        ExitCode = if ($finished) { $process.ExitCode } else { $null }
        DurationSeconds = [math]::Round($clock.Elapsed.TotalSeconds, 3)
        StandardOutput = $stdoutTask.Result
        StandardError = $stderrTask.Result
    }
}

function Get-ScreenshotFailure {
    param([string]$Directory, [string[]]$Expected)
    $shotDir = Join-Path $Directory "gui-shots"
    $actual = if (Test-Path -LiteralPath $shotDir) { @(Get-ChildItem -LiteralPath $shotDir -Filter "*.png" -File | ForEach-Object { $_.BaseName } | Sort-Object) } else { @() }
    $wanted = @($Expected | Sort-Object)
    if ($wanted.Count -eq 0 -and $actual.Count -eq 0) { return "" }
    if ($wanted.Count -eq 0) { return (@($actual | ForEach-Object { ">= $_" }) -join "; ") }
    if ($actual.Count -eq 0) { return (@($wanted | ForEach-Object { "<= $_" }) -join "; ") }
    $difference = @(Compare-Object -ReferenceObject $wanted -DifferenceObject $actual)
    if ($difference.Count -eq 0) { return "" }
    ($difference | ForEach-Object { "$($_.SideIndicator) $($_.InputObject)" }) -join "; "
}

function ConvertTo-NormalizedJsonValue {
    param($Value)
    if ($null -eq $Value) { return $null }
    if ($Value -is [pscustomobject]) {
        $ordered = [ordered]@{}
        foreach ($name in @($Value.PSObject.Properties.Name | Sort-Object)) {
            $ordered[$name] = ConvertTo-NormalizedJsonValue -Value $Value.$name
        }
        return [pscustomobject]$ordered
    }
    if ($Value -is [System.Collections.IEnumerable] -and $Value -isnot [string]) {
        return @($Value | ForEach-Object { ConvertTo-NormalizedJsonValue -Value $_ })
    }
    return $Value
}

function Get-CanonicalJson {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return "" }
    $value = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    return (ConvertTo-NormalizedJsonValue -Value $value | ConvertTo-Json -Depth 20 -Compress)
}

function Invoke-RunnerSelfTest {
    $root = Join-Path $RepoRoot "build/gui-suite-runner-self-test"
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    $messages = [System.Collections.Generic.List[string]]::new()
    $badScript = Join-Path $root "malformed.txt"
    Set-Content -LiteralPath $badScript -Value "unknown-command" -Encoding utf8NoBOM
    try { Get-ScriptFacts -Path $badScript | Out-Null; throw "malformed script was accepted" } catch { if ($_.Exception.Message -like "*malformed script was accepted*") { throw }; $messages.Add("PASS malformed script rejected: $($_.Exception.Message)") }
    $missing = Get-ScreenshotFailure -Directory $root -Expected @("missing-proof")
    if ($missing.Length -eq 0) { throw "missing screenshot was accepted" }
    $messages.Add("PASS missing screenshot rejected: $missing")
    $exitRun = Invoke-CapturedProcess -FilePath "$env:SystemRoot/System32/cmd.exe" -Arguments @("/d", "/c", "exit /b 7") -WorkingDirectory $root -TimeoutSeconds 5
    if ($exitRun.ExitCode -eq 0) { throw "unexpected exit was accepted" }
    $messages.Add("PASS unexpected exit rejected: expected 0, got $($exitRun.ExitCode)")
    $timeoutRun = Invoke-CapturedProcess -FilePath "$env:SystemRoot/System32/WindowsPowerShell/v1.0/powershell.exe" -Arguments @("-NoProfile", "-Command", "Start-Sleep -Seconds 3") -WorkingDirectory $root -TimeoutSeconds 1
    if (-not $timeoutRun.TimedOut) { throw "timeout was accepted" }
    $messages.Add("PASS timeout rejected after 1 second")
    $messages | ForEach-Object { Write-Output $_ }
}

if ($SelfTest) {
    Invoke-RunnerSelfTest
    exit 0
}

if (-not [string]::IsNullOrWhiteSpace($ReviewSummary)) {
    $reviewPath = (Resolve-Path -LiteralPath $ReviewSummary).Path
    $review = Get-Content -LiteralPath $reviewPath -Raw | ConvertFrom-Json
    foreach ($result in @($review.results)) {
        if ($result.review_mode -eq "automatic") { continue }
        $result.visual_review = $ReviewResult
        $result | Add-Member -NotePropertyName visual_review_note -NotePropertyValue $ReviewNote -Force
        if ($ReviewResult -eq "fail") {
            $result.status = "fail"
            $currentFailures = @($result.failures)
            $result.failures = @($currentFailures + "manual visual review failed: $ReviewNote")
        }
    }
    $review | Add-Member -NotePropertyName visual_review_recorded -NotePropertyValue (Get-Date -Format "o") -Force
    $review.passed = @($review.results | Where-Object { $_.status -eq "pass" }).Count
    $review.failed = @($review.results | Where-Object { $_.status -eq "fail" }).Count
    $review | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reviewPath -Encoding utf8NoBOM
    $markdownPath = Join-Path (Split-Path -Parent $reviewPath) "summary.md"
    $markdown = [System.Collections.Generic.List[string]]::new()
    $markdown.Add("# GUI suite run")
    $markdown.Add("")
    $markdown.Add("Group: $($review.group)")
    $markdown.Add("")
    $markdown.Add("Passed: $($review.passed). Failed: $($review.failed). Duration: $($review.total_duration_seconds) seconds.")
    $markdown.Add("")
    $markdown.Add("| Scenario | Result | Seconds | Visual review |")
    $markdown.Add("| --- | --- | ---: | --- |")
    foreach ($result in @($review.results)) { $markdown.Add("| $($result.id) | $($result.status) | $($result.duration_seconds) | $($result.visual_review) |") }
    $markdown | Set-Content -LiteralPath $markdownPath -Encoding utf8NoBOM
    Write-Output "Visual review recorded: $reviewPath"
    exit $(if ($review.failed -gt 0) { 1 } else { 0 })
}

$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
Test-Manifest -Manifest $manifest
Write-Output "Manifest valid: $(@($manifest.scenarios).Count) scripts"
if ($ValidateOnly) { exit 0 }
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw "GUI executable not found: $Executable" }

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$runRoot = Join-Path $RepoRoot "build/gui-suite/$stamp-$Group"
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$selected = @($manifest.scenarios | Where-Object { $Group -in @($_.groups) })
if (-not [string]::IsNullOrWhiteSpace($ScenarioId)) {
    $selected = @($selected | Where-Object { $_.id -eq $ScenarioId })
    if ($selected.Count -ne 1) { throw "scenario is not in group $Group`: $ScenarioId" }
}
$headlessOutput = ""
$headlessResult = $null
$semanticNames = @($selected | ForEach-Object { @($_.semantic_checks) } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
if ($semanticNames.Count -gt 0) {
    if (-not (Test-Path -LiteralPath $HeadlessExecutable -PathType Leaf)) { throw "GUI headless executable not found: $HeadlessExecutable" }
    $headlessDir = Join-Path $runRoot "headless"
    New-Item -ItemType Directory -Path $headlessDir -Force | Out-Null
    $headlessResult = Invoke-CapturedProcess -FilePath $HeadlessExecutable -Arguments @() -WorkingDirectory $headlessDir -TimeoutSeconds 120
    $headlessOutput = $headlessResult.StandardOutput
}

$results = [System.Collections.Generic.List[object]]::new()
foreach ($scenario in $selected) {
    $work = Join-Path $runRoot $scenario.id
    $profile = $manifest.isolation_profiles.($scenario.isolation)
    Write-IsolationProfile -Profile $profile -Directory $work
    $failures = [System.Collections.Generic.List[string]]::new()
    if ("synthetic-catalogue" -in @($profile.fixtures)) {
        $fixtureResult = Invoke-CapturedProcess -FilePath $HeadlessExecutable -Arguments @("--write-synthetic-catalogue") -WorkingDirectory $work -TimeoutSeconds 30
        if ($fixtureResult.TimedOut) { $failures.Add("synthetic catalogue fixture timed out") }
        elseif ($fixtureResult.ExitCode -ne 0) { $failures.Add("synthetic catalogue fixture failed with exit $($fixtureResult.ExitCode)") }
    }
    $prefsPath = Join-Path $work "inop.gui.json"
    $prefsBefore = Get-CanonicalJson -Path $prefsPath
    $scriptPath = Join-Path $RepoRoot $scenario.script
    Write-Output "Running $($scenario.id)"
    $processResult = $null
    $terminalFallback = $false
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        $processResult = Invoke-CapturedProcess -FilePath $Executable -Arguments @("--gui-script", $scriptPath, "--no-color") -WorkingDirectory $work -TimeoutSeconds $scenario.timeout_seconds
        $shotCount = @(Get-ChildItem -LiteralPath (Join-Path $work "gui-shots") -Filter "*.png" -File -ErrorAction SilentlyContinue).Count
        $terminalFallback = @($scenario.screenshots).Count -gt 0 -and $shotCount -eq 0 -and $processResult.StandardOutput -match "choice \[1\]"
        if (-not $terminalFallback) { break }
        if ($attempt -lt 5) { Start-Sleep -Seconds $attempt }
    }
    if ($terminalFallback) { $failures.Add("script entered the terminal before producing screenshots after 5 attempts") }
    if ($processResult.TimedOut) { $failures.Add("timeout after $($scenario.timeout_seconds) seconds") }
    elseif ($processResult.ExitCode -ne $scenario.expected_exit) { $failures.Add("expected exit $($scenario.expected_exit), got $($processResult.ExitCode)") }
    $screenshotFailure = Get-ScreenshotFailure -Directory $work -Expected @($scenario.screenshots)
    if ($screenshotFailure.Length -gt 0) { $failures.Add("screenshot mismatch: $screenshotFailure") }
    foreach ($checkName in @($scenario.semantic_checks)) {
        if ($headlessResult.TimedOut -or $headlessResult.ExitCode -ne 0 -or $headlessOutput -notmatch [regex]::Escape("PASS $checkName")) { $failures.Add("semantic check missing or failed: $checkName") }
    }
    foreach ($assertion in @($scenario.artifact_assertions)) {
        if ($assertion -eq "preferences-created" -and -not (Test-Path -LiteralPath $prefsPath)) { $failures.Add("preferences file was not created") }
        if ($assertion -eq "preferences-unchanged") {
            $prefsAfter = Get-CanonicalJson -Path $prefsPath
            if ($prefsAfter -ne $prefsBefore) { $failures.Add("preference values changed") }
        }
    }
    $status = if ($failures.Count -eq 0) { "pass" } else { "fail" }
    $results.Add([pscustomobject]@{
        id = $scenario.id
        status = $status
        duration_seconds = $processResult.DurationSeconds
        exit_code = $processResult.ExitCode
        timed_out = $processResult.TimedOut
        screenshots = @($scenario.screenshots)
        review_mode = $scenario.review_mode
        visual_review = if ($scenario.review_mode -eq "automatic") { "not-required" } else { "pending" }
        failures = @($failures)
        stdout = $processResult.StandardOutput.Trim()
        stderr = $processResult.StandardError.Trim()
    })
}

$summary = [ordered]@{
    format_version = 1
    group = $Group
    started = $stamp
    manifest = $ManifestPath
    run_directory = $runRoot
    semantic_check_exit = if ($null -eq $headlessResult) { $null } else { $headlessResult.ExitCode }
    total_duration_seconds = [math]::Round((@($results) | Measure-Object -Property duration_seconds -Sum).Sum, 3)
    passed = @($results | Where-Object { $_.status -eq "pass" }).Count
    failed = @($results | Where-Object { $_.status -eq "fail" }).Count
    results = @($results)
}
$summaryPath = Join-Path $runRoot "summary.json"
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding utf8NoBOM
$markdown = [System.Collections.Generic.List[string]]::new()
$markdown.Add("# GUI suite run")
$markdown.Add("")
$markdown.Add("Group: $Group")
$markdown.Add("")
$markdown.Add("Passed: $($summary.passed). Failed: $($summary.failed). Duration: $($summary.total_duration_seconds) seconds.")
$markdown.Add("")
$markdown.Add("| Scenario | Result | Seconds | Visual review |")
$markdown.Add("| --- | --- | ---: | --- |")
foreach ($result in $results) { $markdown.Add("| $($result.id) | $($result.status) | $($result.duration_seconds) | $($result.visual_review) |") }
$markdown | Set-Content -LiteralPath (Join-Path $runRoot "summary.md") -Encoding utf8NoBOM
Write-Output "Summary: $summaryPath"
if ($summary.failed -gt 0) { exit 1 }
