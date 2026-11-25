"""Create the static Windows update feed from a verified public GitHub release JSON.

After publishing and verifying uploads, run this script and commit the output.
No credentials, private endpoints, or embedded authentication are written to the feed.
"""
import argparse
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('release_json', type=Path)
parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'updates/windows-x64.json')
args = parser.parse_args()
release = json.loads(args.release_json.read_text(encoding='utf-8-sig'))
tag = release['tag_name']
if release.get('draft') is not False or not re.fullmatch(r'v?\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?', tag):
    raise SystemExit('Only a published versioned release may enter the update feed')
version = tag.removeprefix('v')
assets = []
for asset in release['assets']:
    name = asset['name']
    if name not in (f'Worldbench-{version}-win-x64.zip', f'Worldbench-{version}-setup.exe'):
        continue
    expected = f'https://github.com/DanielFQZ/Worldbench/releases/download/{tag}/{name}'
    if (asset['browser_download_url'] != expected or asset.get('state') != 'uploaded' or
            not re.fullmatch(r'sha256:[0-9a-f]{64}', asset.get('digest', '')) or
            not 0 < asset['size'] <= 1024**3):
        raise SystemExit(f'Invalid release asset: {name}')
    assets.append({k: asset[k] for k in ('name', 'browser_download_url', 'size', 'digest')})
if len(assets) != 2:
    raise SystemExit('Both installer and ZIP are required')
feed = {'schema': 1, 'releases': [{
    'tag_name': tag, 'draft': False, 'prerelease': bool(release['prerelease']),
    'html_url': f'https://github.com/DanielFQZ/Worldbench/releases/tag/{tag}',
    'body': release.get('body', ''), 'assets': assets}]}
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(feed, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(f'Update feed written for {tag}')
