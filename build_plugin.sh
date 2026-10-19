#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_FILE="$SCRIPT_DIR/DLLMassUnits.uplugin"
ENGINE_ROOT="${UE_ENGINE_PATH:-${UE_PATH:-}}"
OUTPUT_PATH="${1:-$SCRIPT_DIR/Build/Package}"

case "$(uname -s)" in
    Darwin) DEFAULT_PLATFORM="Mac" ;;
    *) DEFAULT_PLATFORM="Linux" ;;
esac

TARGET_PLATFORM="${2:-$DEFAULT_PLATFORM}"
BUILD_CONFIG="${3:-Development}"

if [[ -z "$ENGINE_ROOT" ]]; then
    echo "Error: set UE_ENGINE_PATH to your Unreal Engine root." >&2
    exit 1
fi

if [[ -x "$ENGINE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" ]]; then
    UAT="$ENGINE_ROOT/Engine/Build/BatchFiles/RunUAT.sh"
elif [[ -x "$ENGINE_ROOT/Build/BatchFiles/RunUAT.sh" ]]; then
    UAT="$ENGINE_ROOT/Build/BatchFiles/RunUAT.sh"
else
    echo "Error: RunUAT.sh was not found below '$ENGINE_ROOT'." >&2
    exit 1
fi

if [[ ! -f "$PLUGIN_FILE" ]]; then
    echo "Error: plugin descriptor was not found at '$PLUGIN_FILE'." >&2
    exit 1
fi

echo "Building: $PLUGIN_FILE"
echo "Target: $TARGET_PLATFORM $BUILD_CONFIG"
echo "Package: $OUTPUT_PATH"

"$UAT" BuildPlugin \
    -Plugin="$PLUGIN_FILE" \
    -Package="$OUTPUT_PATH" \
    -TargetPlatforms="$TARGET_PLATFORM" \
    -Configuration="$BUILD_CONFIG"

echo "Build completed successfully: $OUTPUT_PATH"
