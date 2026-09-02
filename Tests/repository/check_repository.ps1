<#
.SYNOPSIS
Checks Atlas documentation links, repository hygiene, and project-owned file headers.

.DESCRIPTION
Major functions:
- Resolve and verify every local Markdown link target.
- Reject raw KiCad sources, obsolete Atlas_Origins paths, and build artifacts.
- Require onboarding documents plus Doxygen file/function tags for App firmware.
- Verify RTOS static-allocation policy and CMake/IAR source membership.

The script is read-only. Build directories and Git internals are excluded so a
developer can run it after compiling without creating false failures.
#>

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$excludedDirectoryPattern = '[\\/](?:\.git|build)(?:[\\/]|$)'
$allFiles = @(Get-ChildItem -LiteralPath $repositoryRoot -Recurse -File |
    Where-Object { $_.FullName -notmatch $excludedDirectoryPattern })
$failures = [System.Collections.Generic.List[string]]::new()

$requiredPaths = @(
    "README.md",
    "STANDARDS.md",
    "CMakeLists.txt",
    "EWARM\Atlas.ewp",
    "docs\PROJECT_STATUS.md",
    "docs\FIRMWARE_ARCHITECTURE.md",
    "docs\BUILDING.md",
    "docs\BRINGUP.md",
    "docs\VALIDATION.md",
    "docs\modules\README.md",
    "docs\reviews\README.md",
    "docs\reviews\REVIEW_1_HARDWARE_PROTOCOL.md",
    "docs\reviews\REVIEW_2_IMPLEMENTATION_BUILD.md",
    "docs\reviews\REVIEW_3_DOCUMENTATION_ONBOARDING.md",
    "docs\reviews\REVIEW_4_RTOS_ARCHITECTURE.md",
    "docs\reviews\REVIEW_5_RTOS_IMPLEMENTATION_BUILD.md",
    "docs\reviews\REVIEW_6_RTOS_DOCUMENTATION_ONBOARDING.md",
    "App\Inc\FreeRTOSConfig.h",
    "App\Inc\atlas_rtos.h",
    "App\Inc\atlas_rtos_policy.h",
    "App\Inc\atlas_time.h",
    "App\Src\atlas_rtos.c",
    "App\Src\atlas_rtos_policy.c",
    "App\Src\atlas_time.c",
    "ThirdParty\FreeRTOS-Kernel\LICENSE",
    "ThirdParty\FreeRTOS-Kernel\README.md"
)
foreach ($relativePath in $requiredPaths) {
    if (-not (Test-Path -LiteralPath (Join-Path $repositoryRoot $relativePath))) {
        $failures.Add("Missing required repository document: $relativePath")
    }
}

$forbiddenKiCadExtensions = @(
    ".kicad_sch", ".kicad_pcb", ".kicad_pro", ".kicad_prl", ".kicad_wks",
    ".kicad_sym", ".kicad_mod", ".kicad_dru", ".kicad_jobset",
    ".sch", ".brd", ".pro"
)
$forbiddenBuildExtensions = @(".elf", ".hex", ".bin", ".o", ".obj", ".su", ".map")
foreach ($file in $allFiles) {
    if (($forbiddenKiCadExtensions -contains $file.Extension.ToLowerInvariant()) -or
        ($file.Name -in "sym-lib-table", "fp-lib-table") -or
        ($file.FullName -match '(?i)Atlas[_ -]?Origins')) {
        $failures.Add("Forbidden raw/obsolete design artifact: $($file.FullName)")
    }
    if ($forbiddenBuildExtensions -contains $file.Extension.ToLowerInvariant()) {
        $failures.Add("Build artifact outside an excluded build directory: $($file.FullName)")
    }
}
$forbiddenKiCadDirectories = @(Get-ChildItem -LiteralPath $repositoryRoot -Recurse -Directory |
    Where-Object { $_.Name -match '(?i)\.pretty$' })
foreach ($directory in $forbiddenKiCadDirectories) {
    $failures.Add("Forbidden raw KiCad footprint directory: $($directory.FullName)")
}

$heapImplementations = @(Get-ChildItem -LiteralPath (
        Join-Path $repositoryRoot "ThirdParty\FreeRTOS-Kernel") -Recurse -File |
    Where-Object { $_.Name -match '^heap_[1-5]\.c$' })
foreach ($file in $heapImplementations) {
    $failures.Add("Dynamic FreeRTOS heap implementation is forbidden: $($file.FullName)")
}

$projectSourceDirectories = @(
    (Join-Path $repositoryRoot "App"),
    (Join-Path $repositoryRoot "Core")
)
$projectSources = @($projectSourceDirectories | ForEach-Object {
    Get-ChildItem -LiteralPath $_ -Recurse -File |
        Where-Object { $_.Extension -in ".c", ".h" }
})
foreach ($file in $projectSources) {
    $content = Get-Content -LiteralPath $file.FullName -Raw
    if ($content -match '\b[A-Za-z_][A-Za-z0-9_]*FromISR\s*\(') {
        $relativeFile = [System.IO.Path]::GetRelativePath($repositoryRoot, $file.FullName)
        $failures.Add("ISR-to-FreeRTOS API requires a fresh priority audit: $relativeFile")
    }
}

$appSourceDirectory = Join-Path $repositoryRoot "App\Src"
foreach ($file in (Get-ChildItem -LiteralPath $appSourceDirectory -File -Filter "*.c")) {
    if ($file.Name -eq "atlas_time.c") {
        continue
    }
    if ((Get-Content -LiteralPath $file.FullName -Raw) -match '\bHAL_Delay\s*\(') {
        $failures.Add("Post-start-capable App driver bypasses AtlasTime_DelayMs(): $($file.Name)")
    }
}

$cmakePath = Join-Path $repositoryRoot "CMakeLists.txt"
$iarPath = Join-Path $repositoryRoot "EWARM\Atlas.ewp"
if ((Test-Path -LiteralPath $cmakePath) -and (Test-Path -LiteralPath $iarPath)) {
    $cmake = (Get-Content -LiteralPath $cmakePath -Raw).Replace('\', '/')
    $iar = (Get-Content -LiteralPath $iarPath -Raw).Replace('\', '/')
    $appSources = @(Get-ChildItem -LiteralPath $appSourceDirectory -File -Filter "*.c")
    foreach ($file in $appSources) {
        $cmakeNeedle = "App/Src/$($file.Name)"
        $iarNeedle = "../App/Src/$($file.Name)"
        if (-not $cmake.Contains($cmakeNeedle)) {
            $failures.Add("App source missing from CMake target: $($file.Name)")
        }
        if (-not $iar.Contains($iarNeedle)) {
            $failures.Add("App source missing from IAR project: $($file.Name)")
        }
    }

    $commonKernelSources = @("list.c", "queue.c", "stream_buffer.c", "tasks.c")
    foreach ($source in $commonKernelSources) {
        $kernelNeedle = "ThirdParty/FreeRTOS-Kernel/$source"
        if (-not $cmake.Contains($kernelNeedle)) {
            $failures.Add("FreeRTOS source missing from CMake target: $source")
        }
        if (-not $iar.Contains("../$kernelNeedle")) {
            $failures.Add("FreeRTOS source missing from IAR project: $source")
        }
    }

    $buildContracts = @(
        @{ Name = "CMake FreeRTOS definition"; Text = $cmake; Needle = "ATLAS_USE_FREERTOS=1" },
        @{ Name = "IAR FreeRTOS definition"; Text = $iar; Needle = "ATLAS_USE_FREERTOS=1" },
        @{ Name = "CMake kernel include"; Text = $cmake; Needle = "ThirdParty/FreeRTOS-Kernel/include" },
        @{ Name = "IAR kernel include"; Text = $iar; Needle = "../ThirdParty/FreeRTOS-Kernel/include" },
        @{ Name = "CMake GNU Cortex-M7 port include"; Text = $cmake; Needle = "ThirdParty/FreeRTOS-Kernel/portable/GCC/ARM_CM7/r0p1" },
        @{ Name = "IAR Cortex-M7 port include"; Text = $iar; Needle = "../ThirdParty/FreeRTOS-Kernel/portable/IAR/ARM_CM7/r0p1" },
        @{ Name = "CMake GNU Cortex-M7 port"; Text = $cmake; Needle = "ThirdParty/FreeRTOS-Kernel/portable/GCC/ARM_CM7/r0p1/port.c" },
        @{ Name = "IAR Cortex-M7 C port"; Text = $iar; Needle = "../ThirdParty/FreeRTOS-Kernel/portable/IAR/ARM_CM7/r0p1/port.c" },
        @{ Name = "IAR Cortex-M7 assembly port"; Text = $iar; Needle = "../ThirdParty/FreeRTOS-Kernel/portable/IAR/ARM_CM7/r0p1/portasm.s" }
    )
    foreach ($contract in $buildContracts) {
        if (-not $contract.Text.Contains($contract.Needle)) {
            $failures.Add("Missing build contract: $($contract.Name)")
        }
    }

    if ($cmake.Contains("ThirdParty/FreeRTOS-Kernel/portable/IAR/")) {
        $failures.Add("CMake target must not compile or include the IAR FreeRTOS port")
    }
    if ($iar.Contains("ThirdParty/FreeRTOS-Kernel/portable/GCC/")) {
        $failures.Add("IAR project must not compile or include the GNU FreeRTOS port")
    }

    try {
        [xml]$iarXml = Get-Content -LiteralPath $iarPath -Raw
        $iarProjectDirectory = Split-Path -Parent $iarPath
        foreach ($node in $iarXml.SelectNodes('//file/name')) {
            $expanded = $node.InnerText.Replace('$PROJ_DIR$', $iarProjectDirectory)
            $referencedPath = [System.IO.Path]::GetFullPath($expanded)
            if (-not (Test-Path -LiteralPath $referencedPath)) {
                $failures.Add("IAR project references a missing file: $($node.InnerText)")
            }
        }
    }
    catch {
        $failures.Add("IAR project XML could not be parsed: $($_.Exception.Message)")
    }
}

$markdownFiles = @($allFiles | Where-Object { $_.Extension -eq ".md" })
$linkPattern = [regex]'!?\[[^\]]*\]\((?<target>[^)]+)\)'
foreach ($file in $markdownFiles) {
    $content = Get-Content -LiteralPath $file.FullName -Raw
    foreach ($match in $linkPattern.Matches($content)) {
        $target = $match.Groups["target"].Value.Trim().Trim('<', '>')
        if (($target -eq "") -or ($target -match '^(?:https?://|mailto:|#)')) {
            continue
        }
        $localTarget = ($target -split '#', 2)[0]
        if ($localTarget -eq "") {
            continue
        }
        $localTarget = [Uri]::UnescapeDataString($localTarget)
        $resolvedTarget = [System.IO.Path]::GetFullPath(
            (Join-Path $file.DirectoryName $localTarget))
        if (-not (Test-Path -LiteralPath $resolvedTarget)) {
            $relativeFile = [System.IO.Path]::GetRelativePath($repositoryRoot, $file.FullName)
            $failures.Add("Broken local Markdown link in ${relativeFile}: $target")
        }
    }
}

$appFiles = @(Get-ChildItem -LiteralPath (Join-Path $repositoryRoot "App") -Recurse -File |
    Where-Object { $_.Extension -in ".c", ".h" })
foreach ($file in $appFiles) {
    $content = Get-Content -LiteralPath $file.FullName -Raw
    $escapedName = [regex]::Escape($file.Name)
    if ($content -notmatch "(?s)^/\*\*.*?@file\s+$escapedName.*?Major functions") {
        $relativeFile = [System.IO.Path]::GetRelativePath($repositoryRoot, $file.FullName)
        $failures.Add("Missing Doxygen file/major-functions header: $relativeFile")
    }
}

# Scan function definitions, not declarations or calls, and require the tags that
# make the implementation navigable without duplicating a full C parser here.
$definitionPattern = '^(?<storage>(?:static|__weak)\s+)?(?:const\s+)?(?<type>void|bool|char|size_t|TickType_t|BaseType_t|UBaseType_t|u?int(?:8|16|32|64)_t|int(?:8|16|32|64)_t|int|float|double|Atlas[A-Za-z0-9_]+|HAL_StatusTypeDef|sh2_Hal_t)\s*(?<pointer>\*+)?\s*(?<name>[A-Za-z_][A-Za-z0-9_]*)\s*\('
foreach ($file in ($appFiles | Where-Object { $_.Extension -eq ".c" })) {
    $lines = @(Get-Content -LiteralPath $file.FullName)
    for ($lineIndex = 0; $lineIndex -lt $lines.Count; $lineIndex++) {
        $definition = [regex]::Match($lines[$lineIndex], $definitionPattern)
        if (-not $definition.Success) {
            continue
        }

        $signature = $lines[$lineIndex]
        $signatureEnd = $lineIndex
        while (($signature -notmatch '\{') -and ($signature -notmatch ';') -and
               ($signatureEnd + 1 -lt $lines.Count)) {
            ++$signatureEnd
            $signature += ' ' + $lines[$signatureEnd]
        }
        if (($signature -notmatch '\{') -or
            (($signature -match ';') -and
             ($signature.IndexOf(';') -lt $signature.IndexOf('{')))) {
            continue
        }

        $functionName = $definition.Groups["name"].Value
        $commentEnd = $lineIndex - 1
        while (($commentEnd -ge 0) -and [string]::IsNullOrWhiteSpace($lines[$commentEnd])) {
            --$commentEnd
        }
        if (($commentEnd -lt 0) -or ($lines[$commentEnd].Trim() -ne "*/")) {
            $failures.Add("Missing adjacent Doxygen block: $($file.Name):$($lineIndex + 1) $functionName")
            $lineIndex = $signatureEnd
            continue
        }

        $commentStart = $commentEnd
        while (($commentStart -ge 0) -and ($lines[$commentStart].Trim() -ne "/**")) {
            --$commentStart
        }
        if ($commentStart -lt 0) {
            $failures.Add("Missing Doxygen opener: $($file.Name):$($lineIndex + 1) $functionName")
            $lineIndex = $signatureEnd
            continue
        }
        $comment = $lines[$commentStart..$commentEnd] -join "`n"
        if ($comment -notmatch '@brief\b') {
            $failures.Add("Missing @brief: $($file.Name):$($lineIndex + 1) $functionName")
        }
        if (($definition.Groups["type"].Value -ne "void") -and
            ($comment -notmatch '@return\b')) {
            $failures.Add("Missing @return: $($file.Name):$($lineIndex + 1) $functionName")
        }

        $openParenthesis = $signature.IndexOf('(')
        $closeParenthesis = $signature.LastIndexOf(')')
        if (($openParenthesis -ge 0) -and ($closeParenthesis -gt $openParenthesis)) {
            $parameterText = $signature.Substring(
                $openParenthesis + 1,
                $closeParenthesis - $openParenthesis - 1).Trim()
            if (($parameterText -ne "") -and ($parameterText -ne "void")) {
                foreach ($parameter in ($parameterText -split ',')) {
                    $parameterMatch = [regex]::Match(
                        $parameter.Trim(),
                        '(?<name>[A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\])?\s*$')
                    if ($parameterMatch.Success) {
                        $parameterName = $parameterMatch.Groups["name"].Value
                        $escapedParameterName = [regex]::Escape($parameterName)
                        if ($comment -notmatch "@param\s+$escapedParameterName\b") {
                            $failures.Add("Missing @param $parameterName`: $($file.Name):$($lineIndex + 1) $functionName")
                        }
                    }
                }
            }
        }
        $lineIndex = $signatureEnd
    }
}

if ($failures.Count -ne 0) {
    foreach ($failure in $failures) {
        Write-Error $failure -ErrorAction Continue
    }
    throw "Repository review failed with $($failures.Count) finding(s)."
}

Write-Host "Atlas repository review passed: links, documents, Doxygen tags, artifact policy, and RTOS build membership."
