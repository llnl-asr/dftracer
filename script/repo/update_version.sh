#!/bin/bash
# Script to update the VERSION file (library version) based on changes since the last tag
# This script analyzes commit messages and file changes to determine version bump type
# 
# Note: This updates the LIBRARY version (VERSION file), not the PACKAGE version (PACKAGE_VERSION file)
# Package version should be managed separately through setuptools-scm

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
VERSION_FILE="${PROJECT_ROOT}/VERSION"

# Print usage
usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Analyze changes since the last tag and update the library VERSION file accordingly.

This script updates the LIBRARY version (VERSION file: e.g., 4.0.0).
For Python PACKAGE version (PACKAGE_VERSION file: e.g., 2.0.2), use setuptools-scm or update manually.

OPTIONS:
    -t, --type TYPE         Force version bump type: major, minor, or patch
    -v, --version VERSION   Set specific version (e.g., 4.1.0)
    -d, --dry-run          Show what would be done without making changes
    -h, --help             Show this help message

VERSION BUMP RULES:
    - MAJOR: Breaking changes, API changes (keywords: BREAKING, API_CHANGE)
    - MINOR: New features, enhancements (keywords: feat, feature, add, new)
    - PATCH: Bug fixes, documentation, minor changes (keywords: fix, bug, doc, chore)

EXAMPLES:
    # Auto-detect version bump from commits
    $0

    # Force a minor version bump
    $0 --type minor

    # Set specific version
    $0 --version 5.0.0

    # Dry run to see what would happen
    $0 --dry-run

EOF
}

# Parse current version
read_version() {
    if [[ ! -f "${VERSION_FILE}" ]]; then
        echo -e "${RED}Error: VERSION file not found at ${VERSION_FILE}${NC}" >&2
        exit 1
    fi
    
    VERSION=$(cat "${VERSION_FILE}" | tr -d '[:space:]')
    
    if [[ ! "${VERSION}" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
        echo -e "${RED}Error: Invalid version format in VERSION file: ${VERSION}${NC}" >&2
        exit 1
    fi
    
    MAJOR="${BASH_REMATCH[1]}"
    MINOR="${BASH_REMATCH[2]}"
    PATCH="${BASH_REMATCH[3]}"
}

# Bump version based on type
bump_version() {
    local bump_type="$1"
    
    case "${bump_type}" in
        major)
            MAJOR=$((MAJOR + 1))
            MINOR=0
            PATCH=0
            ;;
        minor)
            MINOR=$((MINOR + 1))
            PATCH=0
            ;;
        patch)
            PATCH=$((PATCH + 1))
            ;;
        *)
            echo -e "${RED}Error: Invalid bump type: ${bump_type}${NC}" >&2
            exit 1
            ;;
    esac
    
    NEW_VERSION="${MAJOR}.${MINOR}.${PATCH}"
}

# Analyze commits since last tag
analyze_changes() {
    cd "${PROJECT_ROOT}"
    
    # Get the last tag
    LAST_TAG=$(git describe --tags --abbrev=0 2>/dev/null || echo "")
    
    if [[ -z "${LAST_TAG}" ]]; then
        echo -e "${YELLOW}Warning: No previous tags found. Defaulting to patch bump.${NC}" >&2
        echo "patch"
        return
    fi
    
    echo -e "${BLUE}Analyzing changes since tag: ${LAST_TAG}${NC}" >&2
    
    # Get commit messages since last tag
    COMMITS=$(git log ${LAST_TAG}..HEAD --oneline)
    
    if [[ -z "${COMMITS}" ]]; then
        echo -e "${YELLOW}No new commits since last tag ${LAST_TAG}${NC}" >&2
        echo "none"
        return
    fi
    
    echo -e "${BLUE}Commits since ${LAST_TAG}:${NC}" >&2
    echo "${COMMITS}" >&2
    echo "" >&2
    
    # Check for breaking changes (MAJOR bump)
    if echo "${COMMITS}" | grep -iE '(BREAKING|API_CHANGE|breaking change)' > /dev/null; then
        echo -e "${YELLOW}Detected BREAKING CHANGES${NC}" >&2
        echo "major"
        return
    fi
    
    # Check for new features (MINOR bump)
    if echo "${COMMITS}" | grep -iE '(feat|feature|add|new|enhance)' > /dev/null; then
        echo -e "${YELLOW}Detected new FEATURES${NC}" >&2
        echo "minor"
        return
    fi
    
    # Check file changes for significant modifications
    CHANGED_FILES=$(git diff --name-only ${LAST_TAG}..HEAD)
    
    # Major changes: CMakeLists.txt modifications, new directories, API headers
    if echo "${CHANGED_FILES}" | grep -E '(CMakeLists\.txt|include/.*\.h$|src/.*/.*\.h$)' > /dev/null; then
        # Check if API changes
        API_CHANGES=$(git diff ${LAST_TAG}..HEAD -- 'include/*.h' 'include/**/*.h' | grep -E '^\+.*public:|^\+.*class ' || true)
        if [[ -n "${API_CHANGES}" ]]; then
            echo -e "${YELLOW}Detected API changes in headers${NC}" >&2
            echo "minor"
            return
        fi
    fi
    
    # Default to patch bump (bug fixes, docs, minor changes)
    echo -e "${YELLOW}Detected bug fixes or minor changes${NC}" >&2
    echo "patch"
}

# Write new version to file
write_version() {
    local version="$1"
    
    echo "${version}" > "${VERSION_FILE}"
    echo -e "${GREEN}Updated VERSION file to: ${version}${NC}"
}

# Main script
main() {
    local bump_type=""
    local new_version=""
    local dry_run=false
    
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -t|--type)
                bump_type="$2"
                shift 2
                ;;
            -v|--version)
                new_version="$2"
                shift 2
                ;;
            -d|--dry-run)
                dry_run=true
                shift
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                echo -e "${RED}Error: Unknown option: $1${NC}" >&2
                usage
                exit 1
                ;;
        esac
    done
    
    # Read current version
    read_version
    echo -e "${BLUE}Current version: ${VERSION}${NC}"
    
    # Determine new version
    if [[ -n "${new_version}" ]]; then
        # Validate specific version
        if [[ ! "${new_version}" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
            echo -e "${RED}Error: Invalid version format: ${new_version}${NC}" >&2
            exit 1
        fi
        NEW_VERSION="${new_version}"
        echo -e "${BLUE}Setting specific version: ${NEW_VERSION}${NC}"
    elif [[ -n "${bump_type}" ]]; then
        # Force specific bump type
        bump_version "${bump_type}"
        echo -e "${BLUE}Forcing ${bump_type} bump: ${NEW_VERSION}${NC}"
    else
        # Auto-detect bump type
        auto_bump=$(analyze_changes)
        
        if [[ "${auto_bump}" == "none" ]]; then
            echo -e "${GREEN}No changes detected. Version remains: ${VERSION}${NC}"
            exit 0
        fi
        
        bump_version "${auto_bump}"
        echo -e "${BLUE}Auto-detected ${auto_bump} bump: ${NEW_VERSION}${NC}"
    fi
    
    # Apply changes
    if [[ "${dry_run}" == true ]]; then
        echo -e "${YELLOW}[DRY RUN] Would update version from ${VERSION} to ${NEW_VERSION}${NC}"
    else
        write_version "${NEW_VERSION}"
        
        # Show summary
        echo ""
        echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        echo -e "${GREEN}Library version updated successfully!${NC}"
        echo -e "${GREEN}Old version: ${VERSION}${NC}"
        echo -e "${GREEN}New version: ${NEW_VERSION}${NC}"
        echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        echo ""
        echo -e "${BLUE}Next steps:${NC}"
        echo "  1. Review the VERSION file: cat ${VERSION_FILE}"
        echo "  2. Commit the change: git add VERSION && git commit -m 'Bump library version to ${NEW_VERSION}'"
        echo "  3. Create a tag: git tag -a v${NEW_VERSION} -m 'Release v${NEW_VERSION}'"
        echo "  4. Push changes: git push && git push --tags"
        echo ""
        echo -e "${YELLOW}Note: This updated the LIBRARY version (VERSION file).${NC}"
        echo -e "${YELLOW}If you need to update the PACKAGE version (PACKAGE_VERSION file),${NC}"
        echo -e "${YELLOW}do so separately or use setuptools-scm for automatic versioning.${NC}"
    fi
}

main "$@"
