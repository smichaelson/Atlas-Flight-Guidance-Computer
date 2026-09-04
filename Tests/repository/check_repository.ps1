<#
.SYNOPSIS
Checks Atlas documentation links, repository hygiene, and project-owned file headers.

.DESCRIPTION
Major functions:
- Resolve local Markdown targets and ATX-heading anchors outside fenced code.
- Reject raw KiCad sources, obsolete Atlas_Origins paths, and build artifacts.
- Require the consolidated documentation hub and Doxygen tags for App firmware.
- Verify RTOS static-allocation policy and CMake/IAR source membership.

The script is read-only. Build directories and Git internals are excluded so a
developer can run it after compiling without creating false failures.
#>

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$excludedDirectoryPattern = '[\\/](?:\.git|build|\.venv|__pycache__|logs)(?:[\\/]|$)'
$allFiles = @(Get-ChildItem -LiteralPath $repositoryRoot -Recurse -File |
    Where-Object { $_.FullName -notmatch $excludedDirectoryPattern })
$failures = [System.Collections.Generic.List[string]]::new()

function Get-RepositoryRelativePath {
    param([Parameter(Mandatory = $true)][string]$Path)

    # System.IO.Path.GetRelativePath is unavailable in the Windows PowerShell
    # 5.1/.NET Framework environment shipped with many STM32CubeIDE systems.
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $rootPrefix = $repositoryRoot.TrimEnd([char[]]@('\', '/')) +
        [System.IO.Path]::DirectorySeparatorChar
    if ($fullPath.StartsWith($rootPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        return $fullPath.Substring($rootPrefix.Length)
    }
    return $fullPath
}

$requiredPaths = @(
    "README.md",
    "CMakeLists.txt",
    "EWARM\Atlas.ewp",
    "EWARM\stm32h743xx_flash.icf",
    "STM32H743XX_FLASH.ld",
    "docs\README.md",
    "docs\QUICK_START.md",
    "docs\startup.md",
    "App\Inc\atlas_build.h",
    "App\Inc\atlas_bringup.h",
    "App\Inc\atlas_bringup_protocol.h",
    "App\Src\atlas_bringup.c",
    "App\Src\atlas_bringup_protocol.c",
    "tools\bringup\dashboard.py",
    "tools\bringup\protocol.py",
    "tools\bringup\image_check.py",
    "tools\bringup\card_check.py",
    "Tests\bringup\run_bringup_tests.ps1",
    "docs\SYSTEMS.md",
    "docs\PERIPHERALS.md",
    "docs\DEVELOPMENT.md",
    "docs\REVIEW_REPORT.md",
    "docs\reference\RTOS.md",
    "docs\reference\HARDWARE.md",
    "docs\reference\PROVENANCE.md",
    "docs\archive\REVIEW_HISTORY.md",
    "Tests\review\run_review_probes.ps1",
    "Tests\review\test_timing.c",
    "Tests\review\test_gnss_integration.c",
    "Tests\review\test_sd_integration.c",
    "Tests\review\test_analog.c",
    "Tests\review\test_pyro_policy.c",
    "Tests\services\run_service_tests.ps1",
    "Tests\services\test_io.c",
    "Tests\services\test_usb_cdc.c",
    "Tests\services\test_usb_owner.c",
    "Tests\services\test_storage_owner.c",
    "Tests\services\test_expansion.c",
    "Tests\services\test_sysmem.c",
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
$requiredPaths += @(
    "ADXL375", "LSM6DSV16B", "MMC5983MA", "MS5611", "BNO085",
    "GNSS", "BLE", "RFD900X", "LED", "BUZZER"
) | ForEach-Object { "docs\reference\modules\$_.md" }
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
        ($file.FullName -match '(?i)Atlas[_ -]?Origins?')) {
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
    $content = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
    # Inspect kernel API naming, not project adapters such as BSP_SD_DetectFromISR
    # (which only records a media-generation edge and calls no kernel function).
    if ($content -match '\b(?:[xv]|ux|ul)[A-Z][A-Za-z0-9_]*FromISR\s*\(|\bport(?:YIELD_FROM_ISR|END_SWITCHING_ISR)\s*\(') {
        $relativeFile = Get-RepositoryRelativePath -Path $file.FullName
        $failures.Add("ISR-to-FreeRTOS API requires a fresh priority audit: $relativeFile")
    }
}

$appSourceDirectory = Join-Path $repositoryRoot "App\Src"
foreach ($file in (Get-ChildItem -LiteralPath $appSourceDirectory -File -Filter "*.c")) {
    if ($file.Name -eq "atlas_time.c") {
        continue
    }
    if ((Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8) -match '\bHAL_Delay\s*\(') {
        $failures.Add("Post-start-capable App driver bypasses AtlasTime_DelayMs(): $($file.Name)")
    }
    if ((Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8) -match '\b(?:malloc|calloc|realloc|free|[svf]*printf|[svf]*scanf)\s*\(') {
        $failures.Add("App source introduces allocation or libc formatted I/O; review static-memory contract: $($file.Name)")
    }
}

$cmakePath = Join-Path $repositoryRoot "CMakeLists.txt"
$iarPath = Join-Path $repositoryRoot "EWARM\Atlas.ewp"
if ((Test-Path -LiteralPath $cmakePath) -and (Test-Path -LiteralPath $iarPath)) {
    $cmake = (Get-Content -LiteralPath $cmakePath -Raw -Encoding UTF8).Replace('\', '/')
    $iar = (Get-Content -LiteralPath $iarPath -Raw -Encoding UTF8).Replace('\', '/')
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
        [xml]$iarXml = Get-Content -LiteralPath $iarPath -Raw -Encoding UTF8
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

# The main-stack guard is a cross-file contract, not just a map-review note.
$gnuLayout = Get-Content -LiteralPath (Join-Path $repositoryRoot 'STM32H743XX_FLASH.ld') -Raw -Encoding UTF8
$iarLayout = Get-Content -LiteralPath (Join-Path $repositoryRoot 'EWARM\stm32h743xx_flash.icf') -Raw -Encoding UTF8
$mainStartup = Get-Content -LiteralPath (Join-Path $repositoryRoot 'Core\Src\main.c') -Raw -Encoding UTF8
$stackContracts = @(
    @{ Name = 'GNU fixed MSP assertion'; Text = $gnuLayout; Pattern = 'ASSERT\(_sstack\s*==\s*0x2001C000' },
    @{ Name = 'GNU data/guard exclusion'; Text = $gnuLayout; Pattern = 'ASSERT\(_ebss\s*\+\s*_Min_Heap_Size\s*<=\s*__atlas_msp_guard_start__' },
    @{ Name = 'GNU 256-byte guard'; Text = $gnuLayout; Pattern = '__atlas_msp_guard_start__\s*=\s*_sstack\s*-\s*0x100\s*;' },
    @{ Name = 'IAR 16-KiB MSP size'; Text = $iarLayout; Pattern = '__ICFEDIT_size_cstack__\s*=\s*0x4000\s*;' },
    @{ Name = 'IAR fixed MSP base'; Text = $iarLayout; Pattern = '__atlas_msp_start__\s*=\s*0x2001C000\s*;' },
    @{ Name = 'IAR fixed guard base'; Text = $iarLayout; Pattern = '__atlas_msp_guard_start__\s*=\s*0x2001BF00\s*;' },
    @{ Name = 'IAR explicit stack placement'; Text = $iarLayout; Pattern = 'place at address mem:__atlas_msp_start__\s*\{\s*block CSTACK\s*\}' },
    @{ Name = 'IAR writable-data exclusion'; Text = $iarLayout; Pattern = 'define region RAM_region\s*=\s*mem:\[from __ICFEDIT_region_RAM_start__\s+to\s*\(__atlas_msp_guard_start__\s*-\s*1\)\]' },
    @{ Name = 'MPU fixed guard base'; Text = $mainStartup; Pattern = 'MPU_InitStruct\.BaseAddress\s*=\s*0x2001BF00\s*;' },
    @{ Name = 'MPU guard size'; Text = $mainStartup; Pattern = 'MPU_InitStruct\.Size\s*=\s*MPU_REGION_SIZE_256B\s*;' }
)
foreach ($contract in $stackContracts) {
    if ($contract.Text -notmatch $contract.Pattern) {
        $failures.Add("Main-stack/MPU contract needs renewed review: $($contract.Name)")
    }
}

<#
.SYNOPSIS
Removes fenced code from the Markdown subset used by Atlas documentation.
.PARAMETER Content
Complete document text.
.OUTPUTS
String with fence contents replaced by blank lines.
#>
function Get-MarkdownWithoutFences {
    param([AllowEmptyString()][string]$Content)
    $visibleLines = [System.Collections.Generic.List[string]]::new()
    $fenceCharacter = ''
    $fenceLength = 0
    foreach ($line in ($Content -split '\r?\n')) {
        if ($fenceCharacter -ne '') {
            $closingPattern = '^\s{0,3}' + [regex]::Escape($fenceCharacter) +
                              '{' + $fenceLength + ',}\s*$'
            if ($line -match $closingPattern) { $fenceCharacter = '' }
            $visibleLines.Add('')
            continue
        }
        $opening = [regex]::Match($line, '^\s{0,3}(?<fence>\x60{3,}|~{3,})')
        if ($opening.Success) {
            $fence = $opening.Groups['fence'].Value
            $fenceCharacter = $fence[0].ToString()
            $fenceLength = $fence.Length
            $visibleLines.Add('')
        }
        else { $visibleLines.Add($line) }
    }
    return $visibleLines -join [Environment]::NewLine
}

<#
.SYNOPSIS
Builds GitHub-style anchors for Atlas ATX headings, including duplicate suffixes.
.PARAMETER Content
Complete Markdown document; fenced code is ignored.
.OUTPUTS
An ordinal HashSet of heading anchors. This is not a general CommonMark parser.
#>
function Get-MarkdownAnchorSet {
    param([AllowEmptyString()][string]$Content)
    $anchors = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::Ordinal)
    $body = Get-MarkdownWithoutFences -Content $Content
    foreach ($line in ($body -split '\r?\n')) {
        $heading = [regex]::Match($line, '^\s{0,3}#{1,6}\s+(.+?)\s*#*\s*$')
        if (-not $heading.Success) { continue }
        $label = $heading.Groups[1].Value
        $label = [regex]::Replace($label, '!?\[([^\]]*)\]\([^)]+\)', '$1')
        $label = [regex]::Replace($label, '<[^>]+>', '')
        $slug = [regex]::Replace($label.Trim().ToLowerInvariant(),
                                  '[^\p{L}\p{M}\p{N}_\-\s]', '')
        $slug = [regex]::Replace($slug, '\s', '-')
        $candidate = $slug
        $suffix = 0
        while ($anchors.Contains($candidate)) {
            ++$suffix
            $candidate = "$slug-$suffix"
        }
        [void]$anchors.Add($candidate)
    }
    # Prevent PowerShell from enumerating the set into a string array on return.
    return ,$anchors
}

$markdownFiles = @($allFiles | Where-Object { $_.Extension -eq ".md" })
$anchorCache = @{}
$checkedLocalLinkCount = 0
$checkedAnchorCount = 0
$linkPattern = [regex]'!?\[[^\]]*\]\((?<target>[^)]+)\)'
foreach ($file in $markdownFiles) {
    $content = Get-MarkdownWithoutFences -Content (
        Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8)
    foreach ($match in $linkPattern.Matches($content)) {
        $target = $match.Groups["target"].Value.Trim().Trim('<', '>')
        if (($target -eq "") -or ($target -match '^(?:https?://|mailto:)')) {
            continue
        }
        $parts = $target -split '#', 2
        $localTarget = [Uri]::UnescapeDataString($parts[0])
        if ($localTarget -eq '') {
            $resolvedTarget = $file.FullName
        }
        else {
            $resolvedTarget = [System.IO.Path]::GetFullPath(
                (Join-Path $file.DirectoryName $localTarget))
        }
        ++$checkedLocalLinkCount
        $relativeFile = Get-RepositoryRelativePath -Path $file.FullName
        if (-not (Test-Path -LiteralPath $resolvedTarget)) {
            $failures.Add("Broken local Markdown link in ${relativeFile}: $target")
            continue
        }
        if (($parts.Count -eq 2) -and ($parts[1] -ne '') -and
            ([IO.Path]::GetExtension($resolvedTarget) -eq '.md')) {
            ++$checkedAnchorCount
            if (-not $anchorCache.ContainsKey($resolvedTarget)) {
                $anchorCache[$resolvedTarget] = Get-MarkdownAnchorSet -Content (
                    Get-Content -LiteralPath $resolvedTarget -Raw -Encoding UTF8)
            }
            $fragment = [Uri]::UnescapeDataString($parts[1])
            if (-not $anchorCache[$resolvedTarget].Contains($fragment)) {
                $failures.Add("Broken Markdown heading anchor in ${relativeFile}: $target")
            }
        }
    }
}

$appFiles = @(Get-ChildItem -LiteralPath (Join-Path $repositoryRoot "App") -Recurse -File |
    Where-Object { $_.Extension -in ".c", ".h" })
foreach ($file in $appFiles) {
    $content = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
    $escapedName = [regex]::Escape($file.Name)
    if ($content -notmatch "(?s)^/\*\*.*?@file\s+$escapedName.*?Major functions") {
        $relativeFile = Get-RepositoryRelativePath -Path $file.FullName
        $failures.Add("Missing Doxygen file/major-functions header: $relativeFile")
    }
}

# Scan function definitions, not declarations or calls, and require the tags that
# make the implementation navigable without duplicating a full C parser here.
$definitionPattern = '^(?<storage>(?:static|__weak)\s+)?(?:const\s+)?(?<type>void|bool|char|size_t|TickType_t|BaseType_t|UBaseType_t|u?int(?:8|16|32|64)_t|int(?:8|16|32|64)_t|int|float|double|Atlas[A-Za-z0-9_]+|HAL_StatusTypeDef|FRESULT|sh2_Hal_t)\s*(?<pointer>\*+)?\s*(?<name>[A-Za-z_][A-Za-z0-9_]*)\s*\('
foreach ($file in ($appFiles | Where-Object { $_.Extension -eq ".c" })) {
    $lines = @(Get-Content -LiteralPath $file.FullName -Encoding UTF8)
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
        if (($commentEnd -lt 0) -or (-not $lines[$commentEnd].TrimEnd().EndsWith("*/"))) {
            $failures.Add("Missing adjacent Doxygen block: $($file.Name):$($lineIndex + 1) $functionName")
            $lineIndex = $signatureEnd
            continue
        }

        $commentStart = $commentEnd
        while (($commentStart -ge 0) -and ($lines[$commentStart] -notmatch '/\*')) {
            --$commentStart
        }
        if (($commentStart -lt 0) -or ($lines[$commentStart] -notmatch '^\s*/\*\*')) {
            $failures.Add("Missing Doxygen opener: $($file.Name):$($lineIndex + 1) $functionName")
            $lineIndex = $signatureEnd
            continue
        }
        $comment = $lines[$commentStart..$commentEnd] -join "`n"
        if ($comment -notmatch '@brief\b') {
            $failures.Add("Missing @brief: $($file.Name):$($lineIndex + 1) $functionName")
        }
        if ((($definition.Groups["type"].Value -ne "void") -or $definition.Groups["pointer"].Success) -and
            ($comment -notmatch '@return\b')) {
            $failures.Add("Missing @return: $($file.Name):$($lineIndex + 1) $functionName")
        }

        # Ignore inline function bodies when locating the closing signature ')'.
        # Both compact and multiline adjacent Doxygen blocks are valid C/Doxygen.
        $signature = $signature.Substring(0, $signature.IndexOf('{'))
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

Write-Host "Atlas repository review passed: $checkedLocalLinkCount local links, $checkedAnchorCount heading anchors, documents, Doxygen tags, artifact policy, and RTOS build membership."
