[CmdletBinding()]
param(
    [string]$Rom = (Join-Path $env:USERPROFILE 'Downloads/inthunt.7z'),
    [string]$Compiler
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/tools/world_native.ps1"
Import-WorldNativeRom -Root $PSScriptRoot -Rom $Rom
Build-WorldNative -Root $PSScriptRoot -Compiler $Compiler
