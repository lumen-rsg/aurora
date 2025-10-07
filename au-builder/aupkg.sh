#!/bin/bash
#
# aurora-makepkg - Make packages for the Aurora package manager
# Based on the original makepkg script from the Arch Linux team.
#

# --- (Boilerplate, Helpers, Traps are unchanged) ---
export TEXTDOMAIN='pacman-scripts'; export TEXTDOMAINDIR='/usr/share/locale'
export COMMAND_MODE='legacy'; unset CDPATH GREP_OPTIONS
declare -r makepkg_version='6.1.0-aurora'; declare -r confdir='/etc'
declare -r BUILDSCRIPT='PKGBUILD'
MAKEPKG_LIBRARY=${MAKEPKG_LIBRARY:-'/usr/share/makepkg'}; shopt -s extglob
for lib in "$MAKEPKG_LIBRARY"/*.sh; do source "$lib"; done
trap_exit() { local s=$1; shift; if (( ! INFAKEROOT )); then echo; error "$@"; fi; [[ -n "$(jobs -p)" ]] && kill "$(jobs -p)" 2>/dev/null; trap -- "$s"; kill "-$s" "$$"; }
clean_up() { local c=$?; if (( INFAKEROOT )); then return 0; fi; if (( c == 0 && CLEANUP )); then msg "Cleaning up..."; rm -rf "$pkgdirbase" "$srcdir"; fi; exit "$c"; }

# --- (Core Logic functions are unchanged) ---
enter_fakeroot() { msg "Entering fakeroot environment..."; fakeroot -- bash -$- "${BASH_SOURCE[0]}" -F -p "$BUILDFILE" "${ARGLIST[@]}" || exit $?; }
update_pkgver() { msg "Starting pkgver()..."; newpkgver=$(run_function_safe pkgver); if (( $? != 0 )); then error_function pkgver; fi; if ! check_pkgver "$newpkgver"; then error "pkgver() generated an invalid version: %s" "$newpkgver"; exit 1; fi; if [[ -n "$newpkgver" && "$newpkgver" != "$pkgver" ]]; then pkgver="$newpkgver"; pkgrel=1; msg "Version updated to: %s" "$(get_full_version)"; fi; }
error_function() { if (( ! BASH_SUBSHELL )); then error "A failure occurred in %s()." "$1"; plainerr "Aborting..."; fi; exit 1; }
merge_arch_attrs() { local a s=(provides conflicts depends replaces optdepends makedepends checkdepends); for a in "${s[@]}"; do eval "$a+=(\"\${${a}_$CARCH[@]}\")"; done; unset -v "${s[@]/%/_$CARCH}"; }
source_buildfile() { source_safe "$@"; }
run_function_safe() { local rt rs rc; rs=$(shopt -p); rc=$(set +o); local -; set -eE; rt=$(trap -p ERR); trap "error_function '$1'" ERR; run_function "$1" "$2"; trap - ERR; eval "$rt"; eval "$rs"; eval "$rc"; }
run_function() { if [[ -z $1 ]]; then return 1; fi; local p="$1" w="${2:-$srcdir}"; if (( ! BASH_SUBSHELL )); then msg "Starting %s()..." "$p"; fi; cd_safe "$w"; "$p"; }
backup_package_variables() { local v; for v in ${pkgbuild_schema_package_overrides[@]}; do local i="${v}_backup"; eval "$i=(\"\${$v[@]}\")"; done; }
restore_package_variables() { local v; for v in ${pkgbuild_schema_package_overrides[@]}; do local i="${v}_backup"; if [[ -n ${!i} ]]; then eval "$v=(\"\${$i[@]}\")"; else unset "$v"; fi; done; }

# --- REFINED: build_one_package with correct arch handling ---
build_one_package() {
    local pkgname="$1"; msg "Packaging '$pkgname'..."
    local package_func_name=""; local suffix="${pkgname//$pkgbase/}"
    if declare -f "_package$suffix" &>/dev/null; then package_func_name="_package$suffix"
    elif declare -f "package_$pkgname" &>/dev/null; then package_func_name="package_$pkgname"
    elif [[ "$pkgname" == "$pkgbase" ]] && declare -f "_package" &>/dev/null; then package_func_name="_package"
    elif (( ! SPLITPKG )) && declare -f "package" &>/dev/null; then package_func_name="package"
    else error "Packaging function for '$pkgname' not found in PKGBUILD!"; fi

    msg2 "Found package function: ${package_func_name}()"
    pkgdir="$pkgdirbase/$pkgname"; rm -rf "$pkgdir" && mkdir -p "$pkgdir"; run_function_safe "$package_func_name"
    cd_safe "$pkgdir"

    local fullver; fullver=$(get_full_version)
    local pkg_filename="${pkgname}-${fullver}-ARCH.au" # Placeholder for arch

    msg2 "Creating initial package archive..."; (cd "$pkgdir" && tar -cf - . | zstd -T0 - > "$BUILDDIR/$pkg_filename")
    msg2 "Calculating final checksum..."; local final_checksum; final_checksum=$(sha256sum "$BUILDDIR/$pkg_filename" | awk '{print $1}')
    msg2 "Generating final .AURORA_META file..."; local meta_file="$pkgdir/.AURORA_META"

    merge_arch_attrs

    local final_desc="$pkgdesc"; if [[ -z "$final_desc" ]]; then final_desc="$global_desc"; fi

    # --- THIS IS THE CORRECTED ARCH LOGIC ---
    local final_arch
    if [[ -n "$arch" ]]; then # Prioritize arch defined in the package() function
        final_arch="${arch[0]}"
    elif (( ${#global_arch[@]} > 0 )); then # Fallback to global arch from PKGBUILD
        final_arch="${global_arch[0]}"
    else # Final fallback to system arch
        final_arch="$CARCH"
    fi
    # --- END CORRECTION ---

    yq -n ".name = \"$pkgname\" | .version = \"$fullver\" | .arch = \"$final_arch\" | .description = \"$final_desc\" | .checksum = \"$final_checksum\"" > "$meta_file"

    for field in depends conflicts replaces provides; do
        local meta_field="deps"; if [[ "$field" != "depends" ]]; then meta_field="$field"; fi
        if [[ -n "${!field}" ]]; then
            yq -i ".${meta_field} = []" "$meta_file"; local item; for item in "${!field}"; do yq -i ".${meta_field} += [\"${item%%[<>=]*}\"]" "$meta_file"; done; fi
    done

    yq -i ".files = []" "$meta_file"; (cd "$pkgdir" && find . -type f ! -name '.AURORA_META' -printf '%P\n') | while IFS= read -r file; do yq -i ".files += [\"$file\"]" "$meta_file"; done

    msg2 "Updating archive with final metadata..."; (cd "$pkgdir" && tar -cf - . | zstd -T0 - > "$BUILDDIR/$pkg_filename")

    # Rename file to include the final, correct architecture
    local final_pkg_filename="${pkgname}-${fullver}-${final_arch}.au"
    if [[ "$pkg_filename" != "$final_pkg_filename" ]]; then
        mv "$BUILDDIR/$pkg_filename" "$BUILDDIR/$final_pkg_filename"
    fi

    msg "Created package: $final_pkg_filename"
}
usage() { printf "aurora-makepkg (pacman) %s\n" "$makepkg_version"; echo; printf "Make packages for the Aurora package manager\n"; echo; printf "Usage: %s [options]\n" "$0"; echo; printf "Options:\n"; printf "  -c, --clean        Clean up work files after build\n"; printf "  -C, --cleanbuild   Remove \$srcdir before building\n"; printf "  -e, --noextract    Do not extract source files (use existing \$srcdir)\n"; printf "  -f, --force        Overwrite existing package\n"; printf "  -h, --help         Show this help message and exit\n"; printf "  -i, --install      Install package after successful build\n"; printf "  -m, --nocolor      Disable colorized output messages\n"; printf "  -p <file>          Use an alternate build script (instead of 'PKGBUILD')\n"; printf "  -V, --version      Show version information and exit\n"; printf "  --nocheck          Do not run the check() function\n"; printf "  --sign <key>       Sign the resulting package with gpg\n"; printf "  --skipinteg        Do not perform any integrity checks on source files\n"; printf "  --skippgpcheck     Do not verify PGP signatures on source files\n"; }
version() { printf "aurora-makepkg (pacman) %s\n" "$makepkg_version"; }

# --- PROGRAM START ---
umask 0022; ARGLIST=("$@")
OPT_SHORT="cCeD:fFhiLmp:V"; OPT_LONG=('clean' 'cleanbuild' 'noextract' 'force' 'help' 'install' 'nocolor' 'nocheck' 'sign:' 'skipinteg' 'skippgpcheck' 'version')
if ! parseopts "$OPT_SHORT" "${OPT_LONG[@]}" -- "$@"; then exit 1; fi; set -- "${OPTRET[@]}"; unset OPT_SHORT OPT_LONG OPTRET
CLEANBUILD=0; CLEANUP=0; FORCE=0; INFAKEROOT=0; INSTALL=false; NOEXTRACT=0; RUN_CHECKS=true; GPG_SIGN_KEY=""; SKIPPGPCHECK=0
while true; do case "$1" in -c|--clean) CLEANUP=1;; -C|--cleanbuild) CLEANBUILD=1;; -e|--noextract) NOEXTRACT=1;; -f|--force) FORCE=1;; -i|--install) INSTALL=true;; -m|--nocolor) USE_COLOR='n';; -p) shift; BUILDFILE=$1;; --nocheck) RUN_CHECKS=false;; --sign) shift; GPG_SIGN_KEY=$1;; --skipinteg) SKIPCHECKSUMS=1; SKIPPGPCHECK=1;; --skippgpcheck) SKIPPGPCHECK=1;; -F) INFAKEROOT=1;; -h|--help) usage; exit 0;; -V|--version) version; exit 0;; --) shift; break;; esac; shift; done
declare -r startdir="$(pwd -P)"
trap 'clean_up' 0; for s in TERM HUP QUIT; do trap "trap_exit $s \"%s signal caught. Exiting...\" \"$s\"" "$s"; done
trap 'trap_exit INT "Aborted by user! Exiting..."' INT; trap 'trap_exit USR1 "An unknown error has occurred. Exiting..."' ERR
CARCH="x86_64"; CHOST="x86_64-pc-linux-gnu"; CFLAGS="-O2 -pipe -fno-plt -fexceptions -Wp,-D_FORTIFY_SOURCE=2 -Wformat -Werror=format-security -fstack-clash-protection -fcf-protection"; CXXFLAGS="$CFLAGS -Wp,-D_GLIBCXX_ASSERTIONS"; LDFLAGS="-Wl,-O1,--sort-common,--as-needed,-z,relro,-z,now"; RUSTFLAGS="-C opt-level=2"; BUILDENV=(!distcc color !ccache check !sign); OPTIONS=(strip docs !libtool !staticlibs emptydirs zipman !debug); PKGEXT=".au"; SRCEXT=".src.tar.gz"; PACKAGER="Unknown Packager"; SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(date +%s)}; COMPRESSZST=(zstd -c -T0 -); SRCDEST=${SRCDEST:-$startdir}; PKGDEST=${PKGDEST:-$startdir}; SRCPKGDEST=${SRCPKGDEST:-$startdir}; VCSAGENTS=('git::git' 'bzr::bzr' 'hg::hg' 'svn::svn'); CURL_USER_AGENT="aurora-makepkg/$makepkg_version"; DLAGENTS=('ftp::/usr/bin/curl -gqfLC - --ftp-pasv --netrc -o %o %u' "http::/usr/bin/curl -gqfLC - -A \"$CURL_USER_AGENT\" --netrc -o %o %u" "https::/usr/bin/curl -gqfLC - -A \"$CURL_USER_AGENT\" --netrc -o %o %u" 'rsync::/usr/bin/rsync -artvh --no-motd %u %o' 'scp::/usr/bin/scp -C %u %o');
export CARCH CHOST CFLAGS CXXFLAGS LDFLAGS RUSTFLAGS DLAGENTS VCSAGENTS SRCDEST PKGDEST SRCPKGDEST
if [[ -t 1 && $USE_COLOR != "n" ]]; then colorize; else unset ALL_OFF BOLD BLUE GREEN RED YELLOW; fi
if (( EUID == 0 && ! INFAKEROOT )); then error "Running aurora-makepkg as root is not allowed."; exit 1; fi

# --- Shared Setup Logic ---
BUILDFILE=${BUILDFILE:-$BUILDSCRIPT}; if [[ ! -f $BUILDFILE ]]; then error "'%s' does not exist." "$BUILDFILE"; exit 1; fi
if [[ ${BUILDFILE:0:1} != "/" ]]; then BUILDFILE="$startdir/$BUILDFILE"; fi
source_buildfile "$BUILDFILE"
# REFINED: Correctly capture global arch as an array
pkgbase=${pkgbase:-${pkgname[0]}}; global_desc="$pkgdesc"; global_arch=("${arch[@]}"); SPLITPKG=0; if (( ${#pkgname[@]} > 1 )); then SPLITPKG=1; fi
srcdir="$startdir/src"; pkgdirbase="$startdir/pkg"; BUILDDIR="$startdir"

if (( INFAKEROOT )); then
	backup_package_variables
	for pkg in "${pkgname[@]}"; do
		pkgname="$pkg"; build_one_package "$pkg"; restore_package_variables
	done
	msg "Leaving fakeroot environment."; exit 0
fi

# --- Main Execution Flow (Outer Script) ---
command -v yq >/dev/null || error "yq is not installed."
if [[ -n "$GPG_SIGN_KEY" ]]; then command -v gpg >/dev/null || error "gpg not installed."; if ! gpg --list-secret-key "$GPG_SIGN_KEY" &>/dev/null; then error "The key %s does not exist in your keyring." "$GPG_SIGN_KEY"; exit 1; fi; fi
if have_function pkgver; then update_pkgver; fi
basever=$(get_full_version)
msg "Making package: %s %s" "$pkgbase" "$basever"
warning "Dependency checking has been removed. Please ensure you have all 'makedepends' installed."
( mkdir -p "$srcdir"; chmod a-s "$srcdir"; if (( ! NOEXTRACT )); then download_sources; check_source_integrity; if (( CLEANBUILD )); then msg "Removing existing \$srcdir..."; rm -rf "$srcdir"; mkdir -p "$srcdir"; fi; cd_safe "$srcdir"; extract_sources; else warning "Using existing \$srcdir tree..."; fi; if have_function prepare; then run_function_safe "prepare"; fi; if (( ! REPKG )); then if have_function build; then run_function_safe "build"; fi; if [[ "$RUN_CHECKS" == true ]] && have_function check; then run_function_safe "check"; fi; fi; )
msg "Preparing packaging environment..."; rm -rf "$pkgdirbase"; mkdir -p "$pkgdirbase"
enter_fakeroot

for pkgfile in "$startdir"/*.au; do
	[[ -e "$pkgfile" ]] || continue
	if [[ -n "$GPG_SIGN_KEY" ]]; then
		msg "Signing package '$pkgfile'..."; rm -f "$pkgfile.sig"
		gpg --detach-sign --armor -u "$GPG_SIGN_KEY" "$pkgfile"
		mv "$pkgfile.asc" "$pkgfile.sig"
	fi
	if [[ "$INSTALL" == true ]]; then
		msg "Installing '$pkgfile'..."; command -v aurora >/dev/null || error "'aurora' client not found."
		sudo aurora install-local "$pkgfile"
	fi
done

msg "Finished making: %s %s" "$pkgbase" "$(get_full_version)"