# Recon helper: Game.dll/Engine.dll exports -> TSV (rva, mangled, demangled).
# Usage: python3 -I demangle_exports.py <path/to/Module.dll> <out.tsv>
# Needs winedump (wine) and llvm-undname (nix-shell -p llvm).
import shutil, subprocess, sys

undname = shutil.which('llvm-undname') or sys.exit('need llvm-undname (nix-shell -p llvm)')
dump = subprocess.run(['winedump', '-j', 'export', sys.argv[1]], capture_output=True, text=True, check=True).stdout

rows = []
for line in dump.splitlines():
    parts = line.split()
    if len(parts) == 3 and parts[1].isdigit() and len(parts[0]) == 8:
        rows.append((parts[0], parts[2]))

res = subprocess.run([undname, '--no-calling-convention', '--no-access-specifier',
                      '--no-member-type', '--no-variable-type'],
                     input='\n'.join(m for _, m in rows) + '\n', capture_output=True, text=True)
blocks = [b for b in res.stdout.split('\n\n') if b.strip()]
if len(blocks) != len(rows):
    sys.exit(f'demangler output mismatch: {len(blocks)} blocks for {len(rows)} exports')

STR = 'class std::basic_string<{0}, struct std::char_traits<{0}>, class std::allocator<{0}>>'
with open(sys.argv[2], 'w') as out:
    for (rva, mangled), block in zip(rows, blocks):
        lines = block.strip('\n').split('\n')
        dem = lines[-1] if len(lines) > 1 else mangled
        dem = (dem.replace(STR.format('char'), 'std::string')
                  .replace(STR.format('wchar_t'), 'std::wstring')
                  .replace(STR.format('unsigned short'), 'std::u16string'))
        out.write(f'{rva}\t{mangled}\t{dem}\n')
print(len(rows), 'exports')
