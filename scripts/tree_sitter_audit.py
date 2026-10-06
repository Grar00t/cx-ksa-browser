from pathlib import Path
from tree_sitter import Language, Parser
import tree_sitter_cpp

parser = Parser(Language(tree_sitter_cpp.language()))
files = list(Path(r"C:\Users\A\cx-ksa-browser\src").rglob("*.cpp")) + list(Path(r"C:\Users\A\cx-ksa-browser\src").rglob("*.h"))
bad = []
for p in files:
    tree = parser.parse(p.read_bytes())
    if tree.root_node.has_error:
        bad.append(str(p))
print({"files": len(files), "syntax_error_files": bad})
