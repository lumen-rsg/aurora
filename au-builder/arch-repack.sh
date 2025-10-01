#!/usr/bin/env bash
#
# arch-repack.sh - Converts a pre-built Arch Linux package to the Aurora format.
#

# --- Configuration & Colors ---
set -eo pipefail
COLOR_BLUE='\033[1;34m'
COLOR_GREEN='\033[1;32m'
COLOR_RED='\033[1;31m'
COLOR_RESET='\033[0m'

# --- Helper Functions ---
msg() { printf "${COLOR_BLUE}[::repack::]${COLOR_RESET} ${1}\n"; }
error() { printf "${COLOR_RED}ERROR:${COLOR_RESET} ${1}\n" >&2; exit 1; }

# --- Argument & Dependency Check ---
if [[ -z "$1" ]]; then error "No input package specified.\nUsage: ./arch-repack.sh <path-to-arch-package.pkg.tar.zst>"; fi
if ! command -v tar &>/dev/null || ! command -v zstd &>/dev/null || ! command -v awk &>/dev/null; then error "tar, zstd, and awk are required."; fi
INPUT_PKG=$(realpath "$1")
if [[ ! -f "$INPUT_PKG" ]]; then error "Input file not found: $1"; fi

# --- Main Logic ---
WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT
trap 'error "An unexpected error occurred. Cleaning up..."; exit 1' ERR

msg "Unpacking Arch package into temporary directory..."
tar -xf "$INPUT_PKG" -C "$WORKDIR"
cd "$WORKDIR"

if [[ ! -f ".PKGINFO" ]]; then error "This does not appear to be a valid Arch Linux package (.PKGINFO is missing)."; fi

msg "Parsing .PKGINFO metadata..."
pkginfo_get() { grep "^$1 =" .PKGINFO | head -n 1 | sed "s/$1 = //"; }
pkginfo_get_array() { grep "^$1 =" .PKGINFO | sed "s/$1 = //"; }
pkgname=$(pkginfo_get "pkgname"); pkgver=$(pkginfo_get "pkgver" | sed 's/-[0-9.]*$//'); pkgdesc=$(pkginfo_get "pkgdesc"); arch=$(pkginfo_get "arch")
mapfile -t depends < <(pkginfo_get_array "depend"); mapfile -t conflicts < <(pkginfo_get_array "conflict")
mapfile -t replaces < <(pkginfo_get_array "replaces"); mapfile -t provides < <(pkginfo_get_array "provides")

# --- .INSTALL file parsing logic ---
declare -A SCRIPTS_TO_GENERATE
if [[ -f ".INSTALL" ]]; then
    msg "Found .INSTALL script, parsing functions..."
    mkdir -p scripts

    extract_function() {
        local func_name_arg=$1
        awk -v target_func="$func_name_arg" '$0 ~ "^" target_func "\\(\\) \\{" { in_func=1; brace_level=1; next } in_func { brace_level += gsub(/{/, "{"); brace_level -= gsub(/}/, "}"); if (brace_level > 0) print; else in_func=0; }' .INSTALL
    }

    for func in pre_install post_install pre_remove post_remove pre_upgrade post_upgrade; do
        body=$(extract_function "$func")
        if [[ -n "$body" ]]; then
            aurora_key=""
            # --- FIX #1: Handle function redirection (upgrade -> install) ---
            case "$func" in
                pre_upgrade)
                    # If the body is just a call to pre_install, use its body instead.
                    if [[ "$(echo "$body" | xargs)" == "pre_install" ]]; then
                        msg "  -> '${func}()' is a wrapper for 'pre_install()', using its content."
                        body=$(extract_function "pre_install")
                    fi
                    aurora_key="pre_install"
                    ;;
                post_upgrade)
                    # If the body is just a call to post_install, use its body instead.
                    if [[ "$(echo "$body" | xargs)" == "post_install" ]]; then
                        msg "  -> '${func}()' is a wrapper for 'post_install()', using its content."
                        body=$(extract_function "post_install")
                    fi
                    aurora_key="post_install"
                    ;;
                *)
                    aurora_key="$func"
                    ;;
            esac

            # If after all that we still have a body, create the script.
            if [[ -n "$body" ]]; then
                script_path="scripts/${aurora_key}.sh"
                msg "  -> Found content for '${aurora_key}', creating '${script_path}'"
                echo -e "#!/bin/sh\n\n# Extracted from .INSTALL\n" > "$script_path"
                echo "$body" >> "$script_path"
                # --- FIX #2: Make the generated script executable ---
                chmod +x "$script_path"
                SCRIPTS_TO_GENERATE["$aurora_key"]="$script_path"
            fi
        fi
    done
fi

msg "Generating new .AURORA_META file for '${pkgname}'..."
{
    printf "name: \"%s\"\n" "$pkgname"; printf "version: \"%s\"\n" "$pkgver"
    printf "arch: \"%s\"\n" "$arch"; printf "description: \"%s\"\n" "$pkgdesc"
    if [[ "${#depends[@]}" -gt 0 ]]; then printf "deps:\n"; for dep in "${depends[@]}"; do printf "  - %s\n" "$(echo "$dep" | sed 's/[<>=].*//')"; done; fi
    if [[ "${#conflicts[@]}" -gt 0 ]]; then printf "conflicts:\n"; for conf in "${conflicts[@]}"; do printf "  - %s\n" "$conf"; done; fi
    if [[ "${#replaces[@]}" -gt 0 ]]; then printf "replaces:\n"; for rep in "${replaces[@]}"; do printf "  - %s\n" "$rep"; done; fi
    if [[ "${#provides[@]}" -gt 0 ]]; then printf "provides:\n"; for prov in "${provides[@]}"; do printf "  - %s\n" "$prov"; done; fi
    for key in "${!SCRIPTS_TO_GENERATE[@]}"; do printf "%s: \"%s\"\n" "$key" "${SCRIPTS_TO_GENERATE[$key]}"; done
    printf "files:\n"; find . -type f | sed 's|^\./||' | while read -r file; do
        if [[ "$file" != ".PKGINFO" && "$file" != ".MTREE" && "$file" != ".BUILDINFO" && "$file" != ".INSTALL" && "$file" != ".AURORA_META" ]]; then
            printf "  - \\"%s\\"\n" "$file"
        fi
    done
} > .AURORA_META

msg "Removing old Arch Linux metadata..."
rm .PKGINFO .MTREE; rm -f .BUILDINFO .INSTALL

OUTPUT_FILENAME="${pkgname}-${pkgver}.au"
OUTPUT_DIR=$(dirname "$INPUT_PKG")
OUTPUT_PATH="$OUTPUT_DIR/$OUTPUT_FILENAME"
msg "Creating new Aurora package: ${OUTPUT_FILENAME}"
tar --zstd -cf "$OUTPUT_PATH" .
msg "${COLOR_GREEN}Success!${COLOR_RESET} Repacked package created at: ${OUTPUT_PATH}"