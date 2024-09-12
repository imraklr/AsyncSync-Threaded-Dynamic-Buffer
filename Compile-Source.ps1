# Create the target/output directory if it doesn't exist
$targetDir = "src/target/output"
if (-not (Test-Path -Path $targetDir)) {
    New-Item -ItemType Directory -Path $targetDir
}

g++ -std=c++23 src/source/*.cpp -o $targetDir/main
