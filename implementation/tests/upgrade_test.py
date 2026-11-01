"""Upgrade an actual v1.0-created database and verify persistent records survive."""
import argparse
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import tempfile

p=argparse.ArgumentParser();p.add_argument('--old-runtime',required=True);p.add_argument('--new-build',default='build');p.add_argument('--output',default='evidence/upgrade.json');a=p.parse_args()
old=Path(a.old_runtime).resolve();new=Path(a.new_build).resolve();work=new/'upgrade-runtime';work.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=work,ignore_cleanup_errors=True) as tmp:
    root=Path(tmp);database=root/'school.db';files=root/'files'
    def command(exe,schema,*args):
        return subprocess.run([str(exe),'--db',str(database),'--files',str(files),'--schema',str(schema),*args],input='UpgradeTest123!\n',capture_output=True,text=True)
    r=command(old/'lms_server.exe',old/'schema.sql','--init-admin','admin')
    if r.returncode:raise AssertionError(r.stderr)
    with sqlite3.connect(database) as db:
        before=list(db.execute('SELECT * FROM app_user'))
        assert not db.execute("SELECT name FROM sqlite_master WHERE name='class_extension'").fetchall()
    # The new backup invocation opens and migrates the existing database.
    r=command(new/'lms_server.exe',new/'schema.sql','--backup',str(root/'after'))
    if r.returncode:raise AssertionError(r.stderr)
    with sqlite3.connect(database) as db:
        after=list(db.execute('SELECT * FROM app_user'))
        assert before==after
        assert db.execute("SELECT name FROM sqlite_master WHERE name='class_extension'").fetchone()
        assert db.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
    r=command(new/'lms_server.exe',new/'schema.sql','--backup',str(root/'second'))
    assert r.returncode==0
    report={'passed':True,'actual_v1.0_executable_used':True,'existing_account_preserved':before==after,'class_extension_table_created':True,'idempotent_second_run':True,'integrity_check':'ok'}
    Path(a.output).write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
