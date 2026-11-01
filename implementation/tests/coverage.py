"""Collect gcov JSON for the domain and services, excluding vendor and tests."""
import argparse
import gzip
import json
from pathlib import Path
import subprocess
import tempfile
p=argparse.ArgumentParser();p.add_argument('--build',default='build');p.add_argument('--output',default='evidence/coverage.json');a=p.parse_args()
build=Path(a.build).resolve();results=[]
work=build/'coverage-runtime'
work.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='gcov-',dir=work,ignore_cleanup_errors=True) as tmp:
    for stem in ['domain.cpp','services.cpp']:
        notes=list(build.rglob(stem+'.gcno'))+list(build.rglob(stem+'.obj.gcno'))
        if len(notes)!=1:raise RuntimeError(f'Expected one coverage file for {stem}, got {notes}')
        subprocess.run(['gcov','--json-format',str(notes[0])],cwd=tmp,check=True,capture_output=True)
        matches=[]
        for f in Path(tmp).glob('*.gcov.json.gz'):
            data=json.loads(gzip.decompress(f.read_bytes()))
            matches.extend(x for x in data['files'] if Path(x['file']).name==stem)
            f.unlink()
        if len(matches)!=1:raise RuntimeError(f'Missing gcov source {stem}')
        lines=matches[0]['lines'];covered=sum(line['count']>0 for line in lines)
        results.append({'file':stem,'covered_lines':covered,'executable_lines':len(lines),'line_percent':round(covered/len(lines)*100,2)})
covered=sum(r['covered_lines'] for r in results);total=sum(r['executable_lines'] for r in results)
report={'scope':'business logic: src/domain.cpp and src/services.cpp','covered_lines':covered,'executable_lines':total,'line_percent':round(100*covered/total,2),'threshold':70,'passed':covered/total>=.70,'files':results}
Path(a.output).write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
if not report['passed']:raise SystemExit(1)
