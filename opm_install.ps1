# Define the API URL for the latest release
$apiUrl = "https://api.github.com/repos/odizinne/opm/releases/latest"
$downloadFolder = "C:\opm_latest"
$downloadFile = "$downloadFolder\opm.zip"

# Create or clean the download directory
if (Test-Path -Path $downloadFolder) {
    # Remove the existing directory and its contents
    Remove-Item -Path $downloadFolder -Recurse -Force
}
New-Item -ItemType Directory -Path $downloadFolder | Out-Null

# Fetch the latest release information
try {
    # Download the latest release
    $releaseData = Invoke-RestMethod -Uri $apiUrl -Headers @{ 'User-Agent' = 'PowerShell Script' }
    $downloadUrl = $releaseData.assets | Where-Object { $_.name -like "*.zip" } | Select-Object -ExpandProperty browser_download_url

    if (-not $downloadUrl) {
        Write-Error "No zip file found in the latest release assets."
        exit 1
    }

    # Download the ZIP file
    Write-Host "Downloading $downloadUrl..."
    Invoke-WebRequest -Uri $downloadUrl -OutFile $downloadFile

    # Extract the ZIP file
    Write-Host "Extracting $downloadFile..."
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($downloadFile, $downloadFolder)

    # Remove the downloaded ZIP file after extraction
    Remove-Item -Path $downloadFile

    # Run the selfinstall command
    $opmExecutable = "$downloadFolder\opm\opm.exe"
    if (Test-Path -Path $opmExecutable) {
        Write-Host "Running selfinstall..."
        & $opmExecutable selfinstall
    } else {
        Write-Error "Executable not found. Check if the extraction was successful."
    }

} catch {
    Write-Error "An error occurred: $_"
}
