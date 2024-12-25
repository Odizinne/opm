# Define the API URL for the latest release
$apiUrl = "https://api.github.com/repos/odizinne/opm/releases/latest"
$downloadFolder = "$env:TEMP\opm_latest"
$downloadFile = "$downloadFolder\opm.zip"
$appDataFolder = "$env:APPDATA\opm"
$versionFile = "$appDataFolder\version"

# Remove the download directory if it exists
if (Test-Path -Path $downloadFolder) {
    Write-Host "Removing existing directory $downloadFolder..."
    Remove-Item -Path $downloadFolder -Recurse -Force
}

# Create the download directory
New-Item -ItemType Directory -Path $downloadFolder | Out-Null

# Fetch the latest release information
try {
    # Use Invoke-RestMethod to get the latest release data from GitHub
    $releaseData = Invoke-RestMethod -Uri $apiUrl -Headers @{ 'User-Agent' = 'PowerShell Script' }
    
    # Extract the browser_download_url for the asset
    $downloadUrl = $releaseData.assets | Where-Object { $_.name -like "*.zip" } | Select-Object -ExpandProperty browser_download_url
    if (-not $downloadUrl) {
        Write-Error "No zip file found in the latest release assets."
        exit 1
    }

    # Extract version from the download URL
    if ($downloadUrl -match "download/v(\d+)/") {
        $version = $matches[1]
    } else {
        Write-Error "Version not found in download URL."
        exit 1
    }
    
    # Download the ZIP file
    Write-Host "Downloading opm from $downloadUrl..."
    Invoke-WebRequest -Uri $downloadUrl -OutFile $downloadFile
    
    # Extract the ZIP file
    Write-Host "Extracting..."
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($downloadFile, $downloadFolder)
    
    # Remove the downloaded ZIP file after extraction
    Remove-Item -Path $downloadFile
    
    # Run the selfinstall command and wait for it to complete
    $opmExecutable = "$downloadFolder\opm\opm.exe"
    if (Test-Path -Path $opmExecutable) {
        Start-Process -FilePath $opmExecutable -ArgumentList "selfinstall" -Wait -NoNewWindow
    } else {
        Write-Error "Executable not found. Check if the extraction was successful."
    }

    # Create the %APPDATA%\opm directory if it doesn't exist
    if (-not (Test-Path -Path $appDataFolder)) {
        New-Item -ItemType Directory -Path $appDataFolder | Out-Null
    }

    # Write the version to a file
    Set-Content -Path $versionFile -Value $version

} catch {
    Write-Error "An error occurred: $_"
} finally {
    if (Test-Path -Path $downloadFolder) {
        Remove-Item -Path $downloadFolder -Recurse -Force
    }

    # Define the target directory using LOCALAPPDATA\Programs
    $targetDir = "$env:LOCALAPPDATA\Programs\opm"
    
    # Get the current PATH from the registry
    $pathRegistryKey = "HKCU:\Environment"
    $currentPath = (Get-ItemProperty -Path $pathRegistryKey -Name "PATH").PATH
    
    # Check if the target directory is already in the PATH
    if ($currentPath -notlike "*$targetDir*") {
        # If not, add it to the PATH
        $newPath = $currentPath + ";" + $targetDir
        Set-ItemProperty -Path $pathRegistryKey -Name "PATH" -Value $newPath
        Write-Host "Added $targetDir to PATH."
    } else {
        Write-Host "$targetDir is already in PATH."
    }

}

exit
