"""Build actual merge/registry code and exercise the shipped schema41 SQL."""
from pathlib import Path
import subprocess,shlex,tempfile,xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[2]
schema=ET.parse(root/'res/schema.xml').getroot()
sql=next(r.findtext('sql') for r in schema.findall('revision') if r.attrib['version']=='41')
with tempfile.TemporaryDirectory() as temp:
 d=Path(temp);migration=d/'schema.sql';migration.write_text(sql)
 flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','Qt6Core','Qt6Sql'],text=True))
 sources=[root/'os/tests/engine_import_state_probe.cpp',root/'src/library/engine/engineimportmerge.cpp',root/'src/library/engine/engineimportregistry.cpp']
 subprocess.run(['c++','-std=c++20','-O3','-fPIC','-Wall','-Wextra','-Werror',*[str(p) for p in sources],'-I'+str(root/'src'),*flags,'-o',str(d/'probe')],check=True)
 subprocess.run([str(d/'probe'),str(migration)],check=True,timeout=30)
