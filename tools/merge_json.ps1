Param(
    [Parameter(Mandatory=$true)][string]$BaseFile,
    [Parameter(Mandatory=$true)][string]$IncomingFile,
    [Parameter(Mandatory=$true)][string]$OutFile,
    [Parameter(Mandatory=$false)][string]$ArrayKey = "name"  # 对 launch.json 用 name 去重，tasks.json 用 label
)

function Merge-JsonObjects {
    param(
        [pscustomobject]$base,
        [pscustomobject]$incoming,
        [string]$arrayKey
    )

    $result = [ordered]@{}
    foreach ($prop in ($base.PSObject.Properties | Select-Object -ExpandProperty Name)) {
        $result[$prop] = $base.$prop
    }

    foreach ($prop in ($incoming.PSObject.Properties | Select-Object -ExpandProperty Name)) {
        if ($result.Contains($prop)) {
            $a = $result[$prop]
            $b = $incoming.$prop
            if ($a -is [System.Array] -and $b -is [System.Array]) {
                # 合并数组：按 $arrayKey 覆盖（incoming 覆盖 base 的同名项）
                $baseMap = @{}
                $order = @()
                foreach ($item in $a) {
                    if ($item -is [pscustomobject] -and $item.PSObject.Properties.Name -contains $arrayKey) {
                        $k = $item.$arrayKey
                        $baseMap[$k] = $item
                        $order += $k
                    } else {
                        $order += [guid]::NewGuid().ToString()
                        $baseMap[$order[-1]] = $item
                    }
                }
                foreach ($item in $b) {
                    if ($item -is [pscustomobject] -and $item.PSObject.Properties.Name -contains $arrayKey) {
                        $k = $item.$arrayKey
                        if ($baseMap.ContainsKey($k)) {
                            # 覆盖同名项
                            $baseMap[$k] = $item
                        } else {
                            $baseMap[$k] = $item
                            $order += $k
                        }
                    } else {
                        $k = [guid]::NewGuid().ToString()
                        $baseMap[$k] = $item
                        $order += $k
                    }
                }
                $merged = @()
                foreach ($k in $order) { $merged += $baseMap[$k] }
                $result[$prop] = $merged
            } elseif ($a -is [pscustomobject] -and $b -is [pscustomobject]) {
                $result[$prop] = Merge-JsonObjects -base $a -incoming $b -arrayKey $arrayKey
            } else {
                $result[$prop] = $b
            }
        } else {
            $result[$prop] = $incoming.$prop
        }
    }

    return ([pscustomobject]$result)
}

if (-not (Test-Path $BaseFile)) { throw "Base file not found: $BaseFile" }
if (-not (Test-Path $IncomingFile)) { throw "Incoming file not found: $IncomingFile" }

$baseObj = Get-Content -Raw -LiteralPath $BaseFile | ConvertFrom-Json -Depth 64
$incomingObj = Get-Content -Raw -LiteralPath $IncomingFile | ConvertFrom-Json -Depth 64

$merged = Merge-JsonObjects -base $baseObj -incoming $incomingObj -arrayKey $ArrayKey
$json = $merged | ConvertTo-Json -Depth 64
Set-Content -LiteralPath $OutFile -Value $json -NoNewline -Encoding UTF8

Param(
    [Parameter(Mandatory=$true)][string]$BaseFile,
    [Parameter(Mandatory=$true)][string]$IncomingFile,
    [Parameter(Mandatory=$true)][string]$OutFile,
    [Parameter(Mandatory=$false)][string]$ArrayKey = "name"  # 对 launch.json 用 name 去重，tasks.json 用 label
)

function Merge-JsonObjects {
    param(
        [pscustomobject]$base,
        [pscustomobject]$incoming,
        [string]$arrayKey
    )

    $result = [ordered]@{}
    foreach ($prop in ($base.PSObject.Properties | Select-Object -ExpandProperty Name)) {
        $result[$prop] = $base.$prop
    }

    foreach ($prop in ($incoming.PSObject.Properties | Select-Object -ExpandProperty Name)) {
        if ($result.Contains($prop)) {
            $a = $result[$prop]
            $b = $incoming.$prop
            if ($a -is [System.Array] -and $b -is [System.Array]) {
                # 合并数组：按 $arrayKey 去重
                $merged = @()
                $seen = @{}
                foreach ($item in $a + $b) {
                    if ($item -is [pscustomobject] -and $item.PSObject.Properties.Name -contains $arrayKey) {
                        $key = $item.$arrayKey
                        if (-not $seen.ContainsKey($key)) {
                            $merged += $item
                            $seen[$key] = $true
                        }
                    } else {
                        $merged += $item
                    }
                }
                $result[$prop] = $merged
            } elseif ($a -is [pscustomobject] -and $b -is [pscustomobject]) {
                $result[$prop] = Merge-JsonObjects -base $a -incoming $b -arrayKey $arrayKey
            } else {
                $result[$prop] = $b
            }
        } else {
            $result[$prop] = $incoming.$prop
        }
    }

    return ([pscustomobject]$result)
}

if (-not (Test-Path $BaseFile)) { throw "Base file not found: $BaseFile" }
if (-not (Test-Path $IncomingFile)) { throw "Incoming file not found: $IncomingFile" }

$baseObj = Get-Content -Raw -LiteralPath $BaseFile | ConvertFrom-Json -Depth 64
$incomingObj = Get-Content -Raw -LiteralPath $IncomingFile | ConvertFrom-Json -Depth 64

$merged = Merge-JsonObjects -base $baseObj -incoming $incomingObj -arrayKey $ArrayKey
$json = $merged | ConvertTo-Json -Depth 64
Set-Content -LiteralPath $OutFile -Value $json -NoNewline -Encoding UTF8


