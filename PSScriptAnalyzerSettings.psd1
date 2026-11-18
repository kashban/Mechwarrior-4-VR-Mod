@{
    # PSScriptAnalyzer's rules for the PowerShell scripts in tools\ (tools\security\scripts.ps1 and the pipeline use
    # this file). Everything it knows applies, errors and warnings, but for:
    ExcludeRules = @(
        # The scripts talk to the player: a console window (the start script) and a form (the setup). Write-Host is
        # what writes to a console window; nothing here is a pipeline's stage.
        'PSAvoidUsingWriteHost',
        # The functions are the scripts' own steps, called once each by the script - not cmdlets somebody runs with
        # -WhatIf. What the setup changes is said in its window before the button is pressed.
        'PSUseShouldProcessForStateChangingFunctions'
    )
}
