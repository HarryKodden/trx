#!/bin/bash

# TRX Release Script
# This script helps create and push version tags for releases

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_info() {
    echo -e "${GREEN}ℹ️  $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

# Check if we're in a git repository
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    print_error "Not in a git repository"
    exit 1
fi

# Check if working directory is clean
if ! git diff --quiet || ! git diff --staged --quiet; then
    print_error "Working directory is not clean. Please commit or stash changes first."
    exit 1
fi

# Get current version from CMakeLists.txt
CURRENT_VERSION=$(grep "set(CPACK_PACKAGE_VERSION" CMakeLists.txt | sed 's/.*"\(.*\)".*/\1/')
print_info "Current version in CMakeLists.txt: $CURRENT_VERSION"

# Ask for new version
read -p "Enter new version (current: $CURRENT_VERSION): " NEW_VERSION

if [ -z "$NEW_VERSION" ]; then
    print_error "Version cannot be empty"
    exit 1
fi

# Validate version format (basic check)
if ! echo "$NEW_VERSION" | grep -E '^[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9]+)?$' > /dev/null; then
    print_warning "Version '$NEW_VERSION' doesn't match semantic versioning format (x.y.z or x.y.z-prerelease)"
    read -p "Continue anyway? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Update version in CMakeLists.txt
sed -i.bak "s/set(CPACK_PACKAGE_VERSION \"$CURRENT_VERSION\")/set(CPACK_PACKAGE_VERSION \"$NEW_VERSION\")/" CMakeLists.txt
rm CMakeLists.txt.bak

print_info "Updated version in CMakeLists.txt to $NEW_VERSION"

# Commit the version change
git add CMakeLists.txt
git commit -m "Bump version to $NEW_VERSION"

# Create and push tag
TAG="v$NEW_VERSION"
git tag -a "$TAG" -m "Release $NEW_VERSION"
git push origin main
git push origin "$TAG"

print_info "Successfully created and pushed tag $TAG"
print_info "GitHub Actions will now build the release and publish Docker images to GHCR"
print_info "Check https://github.com/HarryKodden/trx/actions for build status"
print_info "Docker images will be available at: ghcr.io/harrykodden/trx:$NEW_VERSION"