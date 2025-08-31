#!/usr/bin/env python3
import sys
import os
import re
import urllib.request
from pathlib import Path
from urllib.parse import urlparse, urlunparse, parse_qs

# --- Robust PKGBUILD Parser ---
class PkgBuildParser:
    def __init__(self, content: str):
        self.content = content
        self.variables = {}
        self._parse_all_variables()

    def _parse_shell_array(self, content: str) -> list[str]:
        words = []; current_word = ""; in_quote = None;
        for char in content:
            if in_quote:
                if char == in_quote: in_quote = None
                else: current_word += char
            elif char in ('"', "'"): in_quote = char
            elif char.isspace():
                if current_word: words.append(current_word); current_word = ""
            else: current_word += char
        if current_word: words.append(current_word)
        return words

    def _parse_all_variables(self):
        array_pattern = re.compile(r'^([a-zA-Z_][a-zA-Z0-9_]+)=\((.*?)\)', re.MULTILINE | re.DOTALL)
        for match in array_pattern.finditer(self.content):
            var_name = match.group(1)
            if 'local ' in self.content[max(0, match.start()-10):match.start()]: continue
            self.variables[var_name] = self._parse_shell_array(match.group(2).strip())
        simple_pattern = re.compile(r'^([a-zA-Z_][a-zA-Z0-9_]+)=(.*)', re.MULTILINE)
        for match in simple_pattern.finditer(self.content):
            var_name = match.group(1)
            if var_name in self.variables: continue
            raw_value = match.group(2).strip()
            if raw_value.startswith('('): continue
            if (raw_value.startswith('"') and raw_value.endswith('"')) or (raw_value.startswith("'") and raw_value.endswith("'")):
                self.variables[var_name] = raw_value[1:-1]
            else: self.variables[var_name] = raw_value

    def _substitute(self, value: str) -> str:
        for var_name, var_value in self.variables.items():
            if isinstance(var_value, str):
                value = value.replace(f'${{{var_name}}}', var_value).replace(f'${var_name}', var_value)
        return value

    def get_value(self, key: str, is_array: bool = False, scope_content=None):
        content_to_search = scope_content if scope_content is not None else self.content
        if is_array:
            match = re.search(fr'{key}=\((.*?)\)', content_to_search, re.DOTALL | re.MULTILINE)
            if not match: return []
            return self._parse_shell_array(self._substitute(match.group(1).strip()))
        else:
            match = re.search(fr'{key}=([\'"]?)(.*?)\1$', content_to_search, re.MULTILINE)
            return self._substitute(match.group(2)) if match else ""

    def extract_function(self, func_name: str) -> str:
        func_name_re = func_name.replace('-', r'\-') # Allow hyphens
        start_pattern = re.compile(fr'^{func_name_re}\(\)\s*{{', re.MULTILINE)
        start_match = start_pattern.search(self.content)
        if not start_match: return ""
        body_start = start_match.end(); brace_level = 1; body_end = body_start
        for char in self.content[body_start:]:
            if char == '{': brace_level += 1
            elif char == '}': brace_level -= 1
            if brace_level == 0: break
            body_end += 1
        body = self.content[body_start:body_end]; lines = body.strip().split('\n')
        if not lines: return ""
        indent = len(lines[0]) - len(lines[0].lstrip())
        return "\n".join(line[indent:] if len(line) > indent else "" for line in lines)

def parse_arch_source(source_str: str) -> dict:
    # ... (This helper function is correct and unchanged)
    result = {}; name_part, *url_part = source_str.split('::', 1)
    if not url_part: url = name_part; name = None
    else: name = name_part; url = url_part[0]
    parsed_url = urlparse(url.replace('git+', '')); clean_url = urlunparse(parsed_url._replace(query='', fragment='')); result['url'] = clean_url
    if "git+" in url:
        result['type'] = 'git'
        if parsed_url.fragment:
            fragment_data = parse_qs(parsed_url.fragment)
            for key, val_list in fragment_data.items():
                if val_list: result[key] = val_list[0]
    else: result['type'] = 'archive'
    if name: result['name'] = name
    elif result['type'] == 'git': result['name'] = Path(clean_url).stem
    else: result['name'] = Path(clean_url).name
    return result

def fetch_pkgbuild(repo_url: str) -> str:
    # ... (This helper function is correct and unchanged)
    if repo_url.endswith("/"): repo_url = repo_url[:-1]
    raw_url = f"{repo_url}/-/raw/main/PKGBUILD"
    print(f"[*] Fetching PKGBUILD from: {raw_url}")
    try:
        with urllib.request.urlopen(raw_url) as response:
            if response.status != 200:
                print(f"Error: Failed to fetch PKGBUILD (HTTP {response.status})", file=sys.stderr); sys.exit(1)
            return response.read().decode('utf-8')
    except urllib.error.URLError as e:
        print(f"Error: Network error fetching PKGBUILD: {e}", file=sys.stderr); sys.exit(1)

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <Arch_Package_Git_URL>", file=sys.stderr); sys.exit(1)
    repo_url = sys.argv[1]; content = fetch_pkgbuild(repo_url); parser = PkgBuildParser(content)

    # --- THIS IS THE NEW SPLIT PACKAGE LOGIC ---
    pkgbase = parser.get_value('pkgbase')
    is_split = bool(pkgbase)

    if is_split:
        print(f"[*] Detected split package with base: {pkgbase}")
        pkg_names = parser.get_value('pkgname', is_array=True)
        output_dir_name = pkgbase
    else:
        pkg_names = [parser.get_value('pkgname')]
        output_dir_name = pkg_names[0]

    # --- Common metadata parsing ---
    pkgver = parser.get_value('pkgver'); pkgdesc = parser.get_value('pkgdesc')
    arch = parser.get_value('arch', is_array=True)
    makedepends = parser.get_value('makedepends', is_array=True)
    sources = parser.get_value('source', is_array=True); sha256sums = parser.get_value('sha256sums', is_array=True)

    print(f"[*] Found base: {output_dir_name} version {pkgver}")
    output_dir = Path(output_dir_name + "-aurora"); output_dir.mkdir(exist_ok=True)
    print(f"[*] Creating build files in: ./{output_dir}/")

    # --- Generate build.yaml ---
    with open(output_dir / "build.yaml", "w") as f:
        if is_split:
            f.write(f"pkgbase: \"{pkgbase}\"\n")
        else:
            f.write(f"name: \"{pkg_names[0]}\"\n")

        f.write(f"version: \"{pkgver}\"\n")
        f.write(f"arch: \"{'any' if 'any' in arch else arch[0] if arch else 'any'}\"\n")
        f.write(f"description: \"{pkgdesc}\"\n")
        if makedepends: f.write("makedepends:\n"); [f.write(f"  - \"{re.split('[<>=]', d)[0]}\"\n") for d in makedepends]
        if sources: f.write("source:\n"); sha_idx = 0
        for src_str in sources:
            parsed = parse_arch_source(src_str)
            f.write(f"  - name: \"{parsed['name']}\"\n"); f.write(f"    type: {parsed['type']}\n"); f.write(f"    url: \"{parsed['url']}\"\n")
            if 'tag' in parsed: f.write(f"    tag: \"{parsed['tag']}\"\n")
            if parsed['type'] == 'archive':
                if sha_idx < len(sha256sums): f.write(f"    sha256sum: \"{sha256sums[sha_idx]}\"\n"); sha_idx += 1

        # If split, write the packages block
        if is_split:
            f.write("packages:\n")
            for name in pkg_names:
                func_name_suffix = name.replace(pkgbase, '')
                func_content = parser.extract_function(f'_package{func_name_suffix}')
                if not func_content: # Fallback for main package
                    func_content = parser.extract_function('_package')

                f.write(f"  - name: \"{name}\"\n")
                sub_desc = parser.get_value('pkgdesc', scope_content=func_content)
                if sub_desc: f.write(f"    description: \"{sub_desc}\"\n")

                for arr_name in ['depends', 'conflicts', 'replaces', 'provides']:
                    sub_arr = parser.get_value(arr_name, is_array=True, scope_content=func_content)
                    if sub_arr:
                        f.write(f"    {arr_name}:\n")
                        [f.write(f"      - \"{re.split('[<>=]', i)[0]}\"\n") for i in sub_arr]
        else:
            depends = parser.get_value('depends', is_array=True)
            if depends:
                f.write("deps:\n")
                [f.write(f"  - \"{re.split('[<>=]', d)[0]}\"\n") for d in depends]

            conflicts = parser.get_value('conflicts', is_array=True)
            if conflicts:
                f.write("conflicts:\n")
                [f.write(f"  - \"{c}\"\n") for c in conflicts]

            replaces = parser.get_value('replaces', is_array=True)
            if replaces:
                f.write("replaces:\n")
                [f.write(f"  - \"{r}\"\n") for r in replaces]

            provides = parser.get_value('provides', is_array=True)
            if provides:
                f.write("provides:\n")
                [f.write(f"  - \"{p}\"\n") for p in provides]

    # --- Generate build.sh ---
    print("[*] Extracting functions...")
    with open(output_dir / "build.sh", "w") as f:
        f.write("#!/bin/bash\n\n# Automatically generated by arch-importer.py\n\n")
        sub = parser._substitute
        for func_name in ['prepare', 'build', 'check']:
            func_content = sub(parser.extract_function(func_name)).replace('$srcdir', '.').replace('${srcdir}', '.')
            if func_content: f.write(f"{func_name}() {{\n{func_content}\n}}\n\n")

        if is_split:
            for name in pkg_names:
                func_name_suffix = name.replace(pkgbase, '')
                func_content = sub(parser.extract_function(f'_package{func_name_suffix}')).replace('$srcdir', '.').replace('$pkgdir', '$PKG_DIR')
                if not func_content: # Fallback for main package
                    func_content = sub(parser.extract_function('_package')).replace('$srcdir', '.').replace('$pkgdir', '$PKG_DIR')
                if func_content: f.write(f"package_{name}() {{\n{func_content}\n}}\n\n")
        else:
            func_content = sub(parser.extract_function('package')).replace('$srcdir', '.').replace('$pkgdir', '$PKG_DIR')
            if func_content: f.write(f"package() {{\n{func_content}\n}}\n\n")

    print(f"\n[+] Success! Build files for '{output_dir_name}' created in '{output_dir}'.")

if __name__ == "__main__":
    main()