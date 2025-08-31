#!/bin/bash
#
# aurora-build - Build Aurora packages from a source directory.
# Supports single and split packages.
#

# --- Configuration & Colors ---
set -eo pipefail
COLOR_BLUE = '\033[1 ; 34m'
COLOR_GREEN = '\033[1 ; 32m'
COLOR_RED = '\033[1 ; 31m'
COLOR_YELLOW = '\033[1 ; 33m'
COLOR_RESET = '\033[0m'

# --- Helper Functions ---
msg() { printf "${COLOR_BLUE}[::aub::]${COLOR_RESET} ${1}\n" ; }
error() { printf "${COLOR_RED}ERROR:${COLOR_RESET} ${1}\n" > & 2 ; exit 1 ; }
warning() { printf "${COLOR_YELLOW}WARNING:${COLOR_RESET} ${1}\n" ; }

# --- Argument Parsing ---
SILENT_MODE = ""
INSTALL_PACKAGE = ""
RUN_CHECKS = 1
for arg in "$@" ; do
    case $arg in
    -i | --install) INSTALL_PACKAGE = 1 ; shift ; ;
    --silent) SILENT_MODE = 1 ; shift ; ;
    --nocheck) RUN_CHECKS = 0 ; shift ; ;
    esac
done

# --- Check Dependencies ---
command -v fakeroot & > /dev/null | | error "fakeroot not installed."
command -v yq & > /dev/null | | error "yq is not installed."

# --- Main Logic ---
BUILD_DIR = $(pwd)
SRC_DIR = "$BUILD_DIR/src" # All sources go into subdirs of src/
AURORA_BUILD_FILE = "$BUILD_DIR/build.yaml"
AURORA_SCRIPT_FILE = "$BUILD_DIR/build.sh"

if [[ ! -f "$AURORA_BUILD_FILE" ]] ; then error "build.yaml not found." ; fi
if [[ ! -f "$AURORA_SCRIPT_FILE" ]] ; then error "build.sh not found." ; fi

# --- Environment Setup ---
msg "Setting up build environment..."
yq_get() { local result = $(yq "$1" "$AURORA_BUILD_FILE" 2 > /dev/null | | true) ; if [[ "$result" = = "null" ]] ; then echo "" ; else echo "$result" ; fi ; }
populate_array() { local -n arr = $1 ; local query = $2 ; local out = $(yq "$query" "$AURORA_BUILD_FILE" | | true) ; if [[ -n "$out" & & "$out" ! = "null" ]] ; then mapfile -t arr < < (echo "$out") ; fi ; }

# Top-level variables
pkgbase = $(yq_get '.pkgbase')
pkgver = $(yq_get '.version')
pkgarch = $(yq_get '.arch')
pkgdesc = $(yq_get '.description')
# If it's not a split package, pkgbase is just the pkgname
if [[ -z "$pkgbase" ]] ; then pkgbase = $(yq_get '.name') ; fi
export pkgbase pkgver pkgarch pkgdesc

# Arrays
declare -a makedepends source sha256sums
populate_array makedepends '.makedepends[]?'
populate_array source '.source[].url'
populate_array sha256sums '.source[].sha256sum?'
export makedepends source sha256sums

# Source the build script to make its functions available
source "$AURORA_SCRIPT_FILE"

# --- Dependency Check ---
msg "Checking build-time dependencies..."
if [[ "${#makedepends[@]}" -gt 0 ]] ; then
    HOST_PM = "" ; MISSING_DEPS = ()
    if command -v pacman & > /dev/null ; then HOST_PM = "pacman" ; elif command -v dnf & > /dev/null ; then HOST_PM = "dnf" ; elif command -v apt-get & > /dev/null ; then HOST_PM = "apt" ; elif command -v aurora & > /dev/null ; then HOST_PM = "aurora" ; fi
    if [[ -z "$HOST_PM" ]] ; then warning "Could not detect host package manager. Skipping check." ; else
    for dep in "${makedepends[@]}" ; do is_installed = 0
    case "$HOST_PM" in pacman) pacman -Q "$dep" & > /dev/null & & is_installed = 1 ; ; dnf) rpm -q "$dep" & > /dev/null & & is_installed = 1 ; ; apt) dpkg -s "$dep" & > /dev/null & & is_installed = 1 ; ; aurora) aurora query "$dep" & > /dev/null & & is_installed = 1 ; ; esac
    if [[ "$is_installed" -eq 0 ]] ; then MISSING_DEPS+ = ("$dep") ; fi
done
if [[ "${#MISSING_DEPS[@]}" -gt 0 ]] ; then
    warning "Missing build dependencies: ${MISSING_DEPS[*]}" ; read -p ":: Proceed with build anyway? [Y/n] " -r confirm
    if [[ "$confirm" = ~ ^[nN]$ ]] ; then msg "Build aborted." ; exit 0 ; fi
    else msg "All build dependencies are satisfied." ; fi
fi
else msg "No build-time dependencies listed." ; fi

# --- Source Handling ---
msg "Handling sources..."
mkdir -p "$SRC_DIR"
SOURCE_COUNT = $(yq '.source | length' "$AURORA_BUILD_FILE") ; if [[ "$SOURCE_COUNT" -eq 0 ]] ; then error "No sources defined" ; fi
for i in "${!source[@]}" ; do
    SOURCE_URL = "${source[$i]}" ; SOURCE_NAME = $(yq_get ".source[$i].name") ; SOURCE_TYPE = $(yq_get ".source[$i].type") ; SOURCE_TAG = $(yq_get ".source[$i].tag") ; SOURCE_SHA256SUM = "${sha256sums[$i]}" ; SOURCE_DEST = "$SRC_DIR/$SOURCE_NAME"
    msg "Processing source '${SOURCE_NAME}'..."
    if [[ "$SOURCE_TYPE" = = "git" ]] ; then
        if [[ -d "$SOURCE_DEST" ]] ; then msg "Git repo found, updating..." ; cd "$SOURCE_DEST" ; git fetch origin ; if [[ -n "$SOURCE_TAG" ]] ; then git checkout "$SOURCE_TAG" ; fi ; cd "$BUILD_DIR"
        else GIT_CMD = "git clone" ; if [[ -n "$SOURCE_TAG" ]] ; then GIT_CMD+ = " --branch $SOURCE_TAG" ; fi ; GIT_CMD+ = " $SOURCE_URL $SOURCE_DEST" ; msg "Cloning..." ; eval "$GIT_CMD" ; fi
        else
        SOURCE_FILENAME = $(basename "$SOURCE_URL") ; if [[ -f "$SOURCE_FILENAME" ]] ; then msg "Source file found, skipping download." ; else msg "Downloading..." ; curl -L -o "$SOURCE_FILENAME" "$SOURCE_URL" ; fi
        if [[ -n "$SOURCE_SHA256SUM" & & "$SOURCE_SHA256SUM" ! = "SKIP" ]] ; then msg "Verifying..." ; sha256sum -c < (echo "$SOURCE_SHA256SUM $SOURCE_FILENAME") | | error "Checksum mismatch!" ; fi
        msg "Extracting..." ; rm -rf "$SOURCE_DEST" ; mkdir -p "$SOURCE_DEST"
        if [[ "$SOURCE_FILENAME" = = *.zip ]] ; then unzip -d "$SOURCE_DEST" "$SOURCE_FILENAME" ; else tar -xf "$SOURCE_FILENAME" -C "$SOURCE_DEST" --strip-components = 1 ; fi
    fi
done

# --- Build Lifecycle ---
PRIMARY_SOURCE_NAME = $(yq_get '.source[0].name') ; PRIMARY_SRC_DIR = "$SRC_DIR/$PRIMARY_SOURCE_NAME"
if declare -f prepare & > /dev/null ; then msg "Starting prepare()..." ; cd "$PRIMARY_SRC_DIR" ; if [[ -n "$SILENT_MODE" ]] ; then prepare & > /dev/null ; else prepare ; fi ; fi
if declare -f build & > /dev/null ; then msg "Starting build()..." ; cd "$PRIMARY_SRC_DIR" ; if [[ -n "$SILENT_MODE" ]] ; then build & > /dev/null ; else build ; fi ; fi
if [[ "$RUN_CHECKS" -eq 1 ]] ; then if declare -f check & > /dev/null ; then msg "Starting check()..." ; cd "$PRIMARY_SRC_DIR" ; if [[ -n "$SILENT_MODE" ]] ; then check & > /dev/null ; else check ; fi ; fi ; fi

# --- Packaging Stage ---
# Determine if we are doing a split or single package build
PACKAGE_NAMES_STR = $(yq '.packages[].name' "$AURORA_BUILD_FILE" | | true)

if [[ -n "$PACKAGE_NAMES_STR" ]] ; then
    # SPLIT PACKAGE
    mapfile -t PACKAGE_NAMES < < (echo "$PACKAGE_NAMES_STR")
    msg "Starting split package build for: ${PACKAGE_NAMES[*]}"

    for i in "${!PACKAGE_NAMES[@]}" ; do
        pkgname = "${PACKAGE_NAMES[$i]}" # Set pkgname for this iteration
        PKG_DIR = "$BUILD_DIR/pkg/$pkgname"
        package_func_name = "package_$pkgname"

        msg "Packaging $pkgname..."
        rm -rf "$PKG_DIR" ; mkdir -p "$PKG_DIR"
        if ! declare -f "$package_func_name" & > /dev/null ; then error "Package function '$package_func_name' not found in build.sh!" ; fi

        export PKG_DIR ; export SILENT_MODE ; export -f "$package_func_name"
        fakeroot bash -c 'cd "$0" ; source "$1" ; if [[ -n "$SILENT_MODE" ]] ; then '"$2"' & > /dev/null ; else '"$2"' ; fi' "$PRIMARY_SRC_DIR" "$AURORA_SCRIPT_FILE" "$package_func_name"

        PKG_FILENAME = "${pkgname}-${pkgver}.pkg.tar.zst"
        msg "Creating package: $PKG_FILENAME" ; cd "$PKG_DIR" ; msg "Generating metadata for $pkgname..."
        {
        printf "name: \"%s\"\n" "$pkgname"
        printf "version: \"%s\"\n" "$pkgver"
        printf "arch: \"%s\"\n" "$(yq_get ".packages[$i].arch // .arch")"
        printf "description: \"%s\"\n" "$(yq_get ".packages[$i].description // .description")"

        # Get deps, conflicts, replaces, and provides specific to this subpackage
        declare -a sub_deps ; populate_array sub_deps ".packages[$i].deps[]?"
        if [[ "${#sub_deps[@]}" -gt 0 ]] ; then
            printf "deps:\n" ; for dep in "${sub_deps[@]}" ; do printf " - %s\n" "$dep" ; done
        fi

        declare -a sub_conflicts ; populate_array sub_conflicts ".packages[$i].conflicts[]?"
        if [[ "${#sub_conflicts[@]}" -gt 0 ]] ; then
            printf "conflicts:\n" ; for conf in "${sub_conflicts[@]}" ; do printf " - %s\n" "$conf" ; done
        fi

        declare -a sub_replaces ; populate_array sub_replaces ".packages[$i].replaces[]?"
        if [[ "${#sub_replaces[@]}" -gt 0 ]] ; then
            printf "replaces:\n" ; for rep in "${sub_replaces[@]}" ; do printf " - %s\n" "$rep" ; done
        fi

        declare -a sub_provides ; populate_array sub_provides ".packages[$i].provides[]?"
        if [[ "${#sub_provides[@]}" -gt 0 ]] ; then
            printf "provides:\n" ; for prov in "${sub_provides[@]}" ; do printf " - %s\n" "$prov" ; done
        fi

        printf "files:\n" ; find . -type f | sed 's | ^\./ | | ' | while read -r f ; do
            if [[ "$f" ! = ".AURORA_META" ]] ; then printf " - \\"%s\\"\n" "$f" ; fi
        done
        } > .AURORA_META
        tar --zstd -cf "$BUILD_DIR/$PKG_FILENAME" .
        cd "$BUILD_DIR" ; msg "Finished package: $PKG_FILENAME"
    done
    else
    # SINGLE PACKAGE
    pkgname = $(yq_get '.name')
    PKG_DIR = "$BUILD_DIR/pkg/$pkgname"
    PKG_FILENAME = "${pkgname}-${pkgver}.pkg.tar.zst"
    msg "Starting package()..." ; rm -rf "$PKG_DIR" ; mkdir -p "$PKG_DIR"
    export PKG_DIR ; export SILENT_MODE ; export -f package
    fakeroot bash -c 'cd "$0" ; source "$1" ; if [[ -n "$SILENT_MODE" ]] ; then package & > /dev/null ; else package ; fi' "$PRIMARY_SRC_DIR" "$AURORA_SCRIPT_FILE"

    msg "Creating package: ${PKG_FILENAME}" ; cd "$PKG_DIR" ; msg "Generating package metadata..."
    {
    printf "name: \"%s\"\n" "$pkgname" ; printf "version: \"%s\"\n" "$pkgver"
    printf "arch: \"%s\"\n" "$pkgarch" ; printf "description: \"%s\"\n" "$pkgdesc"
    if [[ "${#depends[@]}" -gt 0 ]] ; then printf "deps:\n" ; for dep in "${depends[@]}" ; do printf " - %s\n" "$(echo "$dep" | sed 's/[ < > = ].*//')" ; done ; fi
    if [[ "${#replaces[@]}" -gt 0 ]] ; then printf "replaces:\n" ; for rep in "${replaces[@]}" ; do printf " - %s\n" "$rep" ; done ; fi
    if [[ "${#conflicts[@]}" -gt 0 ]] ; then printf "conflicts:\n" ; for conf in "${conflicts[@]}" ; do printf " - %s\n" "$conf" ; done ; fi
    printf "files:\n" ; find . -type f | sed 's | ^\./ | | ' | while read -r f ; do if [[ "$f" ! = ".AURORA_META" ]] ; then printf " - \\"%s\\"\n" "$f" ; fi ; done
    } > .AURORA_META
    tar --zstd -cf "$BUILD_DIR/$PKG_FILENAME" .
    cd "$BUILD_DIR" ; msg "Build finished successfully: ${PKG_FILENAME}"
fi

# --- Optional Install ---
if [[ -n "$INSTALL_PACKAGE" ]] ; then
    msg "Installing package(s)..."
    if ! command -v aurora & > /dev/null ; then error "'aurora' not found." ; fi
    # Find all generated packages and install them
find "$BUILD_DIR" -maxdepth 1 -name "*.pkg.tar.zst" -print0 | xargs -0 -I {} sudo aurora install_local_package {}
fi