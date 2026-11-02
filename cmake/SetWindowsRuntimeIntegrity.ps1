param([Parameter(Mandatory = $true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$accessSection = [Security.AccessControl.AccessControlSections]::Access
$originalAccess = (Get-Acl -LiteralPath $Executable).GetSecurityDescriptorSddlForm($accessSection)
$runtimeAccess = [Security.AccessControl.FileSecurity]::new()
$runtimeAccess.SetSecurityDescriptorSddlForm($originalAccess, $accessSection)
$runtimeAccess.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
    [Security.Principal.WindowsIdentity]::GetCurrent().User,
    [Security.AccessControl.FileSystemRights]::TakeOwnership,
    [Security.AccessControl.AccessControlType]::Allow))
# The owner can edit the DACL, but relabeling also requires WRITE_OWNER.
# Restore that temporary right separately so the new integrity label remains.
try {
    Set-Acl -LiteralPath $Executable -AclObject $runtimeAccess
    & "$env:SystemRoot\System32\icacls.exe" $Executable /setintegritylevel M | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Could not set desktop integrity on $Executable (icacls exit $LASTEXITCODE)."
    }
} finally {
    $restoredAccess = [Security.AccessControl.FileSecurity]::new()
    $restoredAccess.SetSecurityDescriptorSddlForm($originalAccess, $accessSection)
    Set-Acl -LiteralPath $Executable -AclObject $restoredAccess
}
