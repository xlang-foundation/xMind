# Pure formatting of one Process.Modules snapshot; no process or filesystem I/O.
function Get-ProcessModuleObservation([string[]]$Names) {
    @{
        modules=@($Names | Select-Object -First 64)
        moduleCount=$Names.Count
        modulesTruncated=($Names.Count -gt 64)
        modulePresence=@{
            automation=(@($Names | Where-Object {$_ -match '^System\.Management\.Automation(?:\.ni)?\.dll$'}).Count -gt 0)
            utility=(@($Names | Where-Object {$_ -match '^Microsoft\.PowerShell\.Commands\.Utility(?:\.ni)?\.dll$'}).Count -gt 0)
            management=(@($Names | Where-Object {$_ -match '^Microsoft\.PowerShell\.Commands\.Management(?:\.ni)?\.dll$'}).Count -gt 0)
            stateRepository=(@($Names | Where-Object {$_ -match '^Windows\.StateRepositoryPS\.dll$'}).Count -gt 0)
            amsi=(@($Names | Where-Object {$_ -match '^amsi\.dll$'}).Count -gt 0)
        }
    }
}
