from pathlib import Path
import tempfile,subprocess,sys,json,hashlib,concurrent.futures,os
root=Path(__file__).resolve().parents[1]; temp=Path(tempfile.mkdtemp(prefix='Worldbench-silent-tests-')); ps='powershell.exe'
def monitor_case(code,version,valid=True):
 case=temp/f'case-{code}-{version}-{valid}';case.mkdir();exe=case/'Worldbench.exe';exe.write_bytes(b'test fixture')
 (case/'build-info.json').write_text(json.dumps({'version':version,'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest() if valid else '0'*64}))
 process=subprocess.Popen([sys.executable,'-c',f'import time;time.sleep(8);raise SystemExit({code})'])
 r=subprocess.run([ps,'-NoProfile','-ExecutionPolicy','Bypass','-File',str(root/'app/updates/WorldbenchInstallMonitor.ps1'),'-InstallerProcessId',str(process.pid),'-InstallDir',str(case),'-ExpectedVersion','0.26.4-pre.5','-LogPath',str(case/'install.log'),'-NoRestart','-NonInteractive'],capture_output=True,timeout=30)
 process.wait(); expected=0 if code==0 and version=='0.26.4-pre.5' and valid else 1
 assert r.returncode==expected,(code,version,r.stdout,r.stderr)
 return f'monitor exit={code} version={version} hash={valid}: PASS'
with concurrent.futures.ThreadPoolExecutor() as pool:
 for r in pool.map(lambda x:monitor_case(*x),[(0,'0.26.4-pre.5',True),(5,'0.26.4-pre.5',True),(0,'old',True),(0,'0.26.4-pre.5',False)]):print(r)
# Compile only the shipped Pascal control flow into a harmless non-elevating
# fixture. No production registry, shortcuts, uninstaller or installation path.
source=root/'scripts/Worldbench.iss';code=source.read_text(encoding='utf-8-sig').split('[Code]\n',1)[1]
fixture=temp/'fixture';fixture.mkdir();(fixture/'Worldbench.exe').write_bytes(b'new fixture')
iss=temp/'fixture.iss'; out=temp/'out'; dest=temp/'test install';dest.mkdir();(dest/'Worldbench.exe').write_bytes(b'old fixture')
iss.write_text(f'''[Setup]
AppName=Worldbench isolated installer check
AppVersion=0.26.4
AppId=WorldbenchIsolatedSilentFixture
DefaultDirName={dest}
PrivilegesRequired=lowest
Uninstallable=no
CreateAppDir=yes
DirExistsWarning=no
OutputDir={out}
OutputBaseFilename=fixture
[Files]
Source: "{fixture}\\Worldbench.exe"; DestDir: "{{app}}"; Flags: ignoreversion
[Code]
'''+code,encoding='utf-8-sig')
iscc=Path(os.environ['LOCALAPPDATA'])/'Programs/Inno Setup 6/ISCC.exe'
if not iscc.exists():iscc=Path('C:/Program Files (x86)/Inno Setup 6/ISCC.exe')
r=subprocess.run([str(iscc),'/Q',str(iss)],capture_output=True,text=True);assert r.returncode==0,(r.stdout,r.stderr)
setup=out/'fixture.exe';r=subprocess.run([str(setup),'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',f'/DIR={dest}','/WBAUTOUPDATE=1','/WBPID=0',f'/LOG={temp}/rejected.log'],timeout=20)
assert r.returncode!=0 and (dest/'Worldbench.exe').read_bytes()==b'old fixture'
print('invalid automatic-update PID rejected without overwrite: PASS')
# Exercise the production wait-before-write path using a disposable process.
process=subprocess.Popen([sys.executable,'-c','import time;time.sleep(3)'])
r=subprocess.run([str(setup),'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',f'/DIR={dest}','/WBAUTOUPDATE=1',f'/WBPID={process.pid}',f'/LOG={temp}/success.log'],timeout=25)
process.wait()
assert r.returncode==0 and (dest/'Worldbench.exe').read_bytes()==b'new fixture'
print('silent existing-directory overwrite after process exit: PASS')
print('Evidence:',temp)
