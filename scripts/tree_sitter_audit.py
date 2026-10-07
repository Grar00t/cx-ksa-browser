from pathlib import Path
from tree_sitter import Language, Parser
import tree_sitter_cpp

parser = Parser(Language(tree_sitter_cpp.language()))
repo_root = Path(__file__).resolve().parents[1]
source_root = repo_root / "src"
files = sorted(source_root.rglob("*.cpp")) + sorted(source_root.rglob("*.h"))
if not files:
    raise SystemExit(f"No C/C++ source files found under {source_root}")

bad = []
for path in files:
    tree = parser.parse(path.read_bytes())
    if tree.root_node.has_error:
        bad.append(str(path.relative_to(repo_root)))

print({"files": len(files), "syntax_error_files": bad})
if bad:
    raise SystemExit(1)
