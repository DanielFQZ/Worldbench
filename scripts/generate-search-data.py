"""Generate bundled pinyin search data using pypinyin==0.55.0 (MIT)."""
import json
from pathlib import Path
from importlib.metadata import distribution
from pypinyin import lazy_pinyin, Style

root = Path(__file__).resolve().parents[1]
names = json.loads((root / "src/core/resources/zh_cn_blocks_1.20.1.json").read_text(encoding="utf-8"))
titles = {}
for title in sorted(set(names.values())):
    syllables = lazy_pinyin(title, style=Style.NORMAL)
    titles[title] = ["".join(syllables), "".join(s[0] for s in syllables if s)]
characters = {}
for point in range(0x3400, 0xa000):
    char = chr(point)
    syllable = lazy_pinyin(char, style=Style.NORMAL)[0]
    if syllable != char:
        characters[char] = syllable
out = root / "src/core/resources/pinyin_search.json"
out.write_text(json.dumps({"titles": titles, "characters": characters}, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8", newline="\n")
dist = distribution("pypinyin")
license_file = next(f for f in dist.files if str(f).endswith("licenses/LICENSE.txt"))
(root / "licenses/pypinyin-MIT.txt").write_bytes(dist.locate_file(license_file).read_bytes())
print(len(titles), "titles;", len(characters), "character readings")
