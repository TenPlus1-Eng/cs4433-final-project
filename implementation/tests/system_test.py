"""Real HTTP workflow, negative inputs, crash recovery and backup checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import sqlite3
import shutil
import subprocess
import tempfile
import time
import urllib.request
import urllib.error

parser = argparse.ArgumentParser()
parser.add_argument('--build', default='build')
parser.add_argument('--output', default='evidence/system.json')
args = parser.parse_args()
build = Path(args.build).resolve()
exe = build / ('lms_server.exe' if os.name == 'nt' else 'lms_server')
records = []

def check(name, condition):
    records.append({'test': name, 'passed': bool(condition)})
    if not condition:
        raise AssertionError(name)

work = build / 'test-runtime'
work.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='system-', dir=work, ignore_cleanup_errors=True) as directory:
    root = Path(directory)
    common = [str(exe), '--db', str(root/'school.db'), '--files', str(root/'files'), '--schema', str(build/'schema.sql')]
    init = subprocess.run(common+['--init-admin', 'admin'], input='Admin123!\n', text=True, capture_output=True)
    if init.returncode: print('BOOTSTRAP FAILED', init.returncode, init.stdout, init.stderr)
    check('S01 bootstrap administrator', init.returncode == 0)
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        port = s.getsockname()[1]
    def start():
        proc = subprocess.Popen(common+['--port', str(port)], stdout=open(root/'server.log','a'), stderr=subprocess.STDOUT)
        for _ in range(200):
            try:
                with socket.create_connection(('127.0.0.1', port), .1):
                    return proc
            except OSError:
                time.sleep(.05)
        proc.kill()
        proc.wait()
        print((root/'server.log').read_text())
        raise RuntimeError('Server failed to start')
    process = start()
    def request(method, path, data=None, token=None, raw=None, content_type='application/json'):
        body = raw if raw is not None else (json.dumps(data).encode() if data is not None else None)
        headers = {'Content-Type': content_type}
        if token:
            headers['Authorization'] = 'Bearer '+token
        req = urllib.request.Request(f'http://127.0.0.1:{port}'+path, data=body, headers=headers, method=method)
        try:
            response = urllib.request.urlopen(req, timeout=120)
        except urllib.error.HTTPError as e:
            response = e
        return response.code, json.loads(response.read())
    def ok(method, path, data=None, token=None, **kw):
        status, value = request(method, path, data, token, **kw)
        check(f'{method} {path}', status == 200)
        return value
    def login(name):
        return ok('POST', '/api/login', {'username':name, 'password':'Admin123!'})['token']
    try:
        admin = login('admin')
        ids = {}
        for name, role in [('student','STUDENT'),('lecturer','LECTURER'),('hod','HEAD_OF_DEPARTMENT'),('other','STUDENT'),('otherhod','HEAD_OF_DEPARTMENT')]:
            ids[name] = ok('POST','/api/admin/users',{'username':name,'full_name':name,'role':role,'password':'Admin123!'},admin)['id']
        dep = ok('POST','/api/admin/departments',{'name':'Science','head_user_id':ids['hod']},admin)['id']
        course = ok('POST','/api/admin/courses',{'code':'CS4433','title':'SE','department_id':dep},admin)['id']
        ok('PUT',f'/api/admin/courses/{course}/lecturers',{'lecturer_id':ids['lecturer']},admin)
        ok('PUT',f'/api/admin/courses/{course}/enrolments',{'student_id':ids['student']},admin)
        teacher, student, hod, other, outsider = (login(n) for n in ['lecturer','student','hod','other','otherhod'])
        assignment = ok('POST',f'/api/courses/{course}/assignments',{'title':'Project','description':'Test','deadline':'2099-01-01T10:00:00Z','max_mark':50},teacher)['id']
        ok('PUT',f'/api/assignments/{assignment}/class-extension',{'new_deadline':'2100-01-01T10:00:00Z','reason':'School closure'},teacher)
        listing=ok('GET','/api/student/courses',token=student)
        check('S21 CR1 class extension visible',listing[0]['assignments'][0]['effective_deadline']=='2100-01-01T10:00:00Z' and listing[0]['assignments'][0]['class_extension']['reason']=='School closure')
        payload = b'\x00\xff\x01coursework\r\n'
        boundary='LMS_TEST_BOUNDARY'
        raw=(f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="work.bin"\r\nContent-Type: application/octet-stream\r\n\r\n'.encode()+payload+f'\r\n--{boundary}--\r\n'.encode())
        receipt=ok('POST',f'/api/assignments/{assignment}/submission',token=student,raw=raw,content_type=f'multipart/form-data; boundary={boundary}')
        sid=receipt['id']
        check('S02 multipart SHA256 matches upload',receipt['sha256']==hashlib.sha256(payload).hexdigest())
        downloaded=ok('GET',f'/api/submissions/{sid}/files/1',token=teacher)
        check('S03 binary download round trip',bytes.fromhex(downloaded['hex'])==payload)
        status,_=request('GET',f'/api/submissions/{sid}/files/1',token=other)
        check('S04 student cannot download another student file',status==403)
        ok('PUT',f'/api/submissions/{sid}/mark',{'version':1,'raw_mark':40,'feedback':'Good work'},teacher)
        hidden=ok('GET','/api/student/results',token=student)
        check('S05 mark and feedback hidden before release','raw_mark' not in hidden[0] and 'feedback' not in hidden[0])
        status,_=request('POST',f'/api/submissions/{sid}/approve',{'version':2},outsider)
        check('S06 other department cannot moderate',status==403)
        ok('PUT',f'/api/submissions/{sid}/adjust',{'version':2,'raw_mark':38,'reason':'Rubric correction'},hod)
        released=ok('POST',f'/api/assignments/{assignment}/release',{},hod)
        check('S07 approved mark released',released['released']==[sid])
        shown=ok('GET','/api/student/results',token=student)
        check('S08 final result and feedback visible',shown[0]['final_mark']==38 and shown[0]['feedback']=='Good work')
        audit=ok('GET',f'/api/hod/audit?submission={sid}',token=hod)
        check('S09 exactly one audit per mark mutation',len(audit)==2 and audit[1]['old_raw']==40 and audit[1]['new_raw']==38)
        negatives=[('POST','/api/login',b'{',None),('POST','/api/login',b'{}',None),('GET','/api/student/courses',None,None),('POST',f'/api/courses/{course}/assignments',b'{}',student)]
        for i in range(30):
            if i<len(negatives):
                method,path,body,token=negatives[i]
            else:
                method,path,body,token='PUT',f'/api/submissions/{sid}/mark',json.dumps({'version':-i,'raw_mark':i*100,'feedback':''}).encode(),teacher
            code,_=request(method,path,token=token,raw=body)
            check(f'S10 invalid/fault input {i+1:02}',400<=code<500)
        check('S11 server remains responsive after invalid inputs',request('GET','/api/student/courses',token=student)[0]==200)
        with sqlite3.connect(root/'school.db') as db:
            for statement in ['UPDATE mark_audit SET reason="tamper"','DELETE FROM mark_audit','DELETE FROM app_user WHERE id=2']:
                try:
                    db.execute(statement)
                    raise AssertionError('Constraint accepted '+statement)
                except sqlite3.IntegrityError:
                    records.append({'test':'S12 '+statement.split()[0]+' rejected', 'passed':True})
            hashes=[r[0] for r in db.execute('SELECT password_hash FROM app_user')]
            check('S13 unique Argon2id hashes',len(set(hashes))==len(hashes) and all(h.startswith('$argon2id$') for h in hashes))
        console=build/('lms_client.exe' if os.name=='nt' else 'lms_client')
        client_run=subprocess.run([str(console),f'http://127.0.0.1:{port}'],input='student\nAdmin123!\n1\n3\n6\nquit\n',capture_output=True,text=True,timeout=30)
        check('S22 real console client lists assignments and released results',client_run.returncode==0 and 'Good work' in client_run.stdout and 'School closure' in client_run.stdout and 'Awaiting results' not in client_run.stdout)
        configured=root/'configured-client'
        configured.mkdir()
        configured_exe=configured/console.name
        shutil.copy2(console,configured_exe)
        (configured/'server.url').write_text(f'http://127.0.0.1:{port}\n')
        default_run=subprocess.run([str(configured_exe)],cwd=root,input='student\nAdmin123!\n3\n6\nquit\n',capture_output=True,text=True,timeout=30)
        check('S23 console without arguments reads server.url beside executable',default_run.returncode==0 and 'Good work' in default_run.stdout and f'Server: http://127.0.0.1:{port}' in default_run.stdout)
        for name,title,first,last in [('admin','Administrator','List users',14),('student','Student','My courses and assignments',6),('lecturer','Lecturer','Create assignment',8),('hod','Head of Department','Department submissions',9)]:
            menu_run=subprocess.run([str(console),f'http://127.0.0.1:{port}'],input=f'{name}\nAdmin123!\nmenu\n99\n{last}\nquit\n',capture_output=True,text=True,timeout=30)
            check(f'S24 {title} always shows numbered options after help and invalid selection',menu_run.returncode==0 and menu_run.stdout.count(title+' - Main menu')>=3 and f'1. {first}' in menu_run.stdout and f'{last}. Log out' in menu_run.stdout)
        create_run=subprocess.run([str(console),f'http://127.0.0.1:{port}'],input='admin\nAdmin123!\n2\nconsole_student\nConsole Student\nunknown\n1\nConsole123!\n14\nquit\n',capture_output=True,text=True,timeout=30)
        check('S25 create user displays all role choices and retries an invalid role',create_run.returncode==0 and all(label in create_run.stdout for label in ['1. Student','2. Lecturer','3. Head of Department','4. Administrator','Please choose one of the four roles shown above.']))
        created=[u for u in ok('GET','/api/admin/users',token=admin) if u['username']=='console_student']
        check('S25 numeric role choice creates the correct server-side role',len(created)==1 and created[0]['role']=='STUDENT')
        update_run=subprocess.run([str(console),f'http://127.0.0.1:{port}'],input=f"admin\nAdmin123!\n3\n{created[0]['id']}\nConsole Student Updated\nmaybe\nyes\n14\nquit\n",capture_output=True,text=True,timeout=30)
        check('S26 yes/no prompt shows its choices and retries invalid input',update_run.returncode==0 and '(yes/no)' in update_run.stdout and 'Please enter yes or no.' in update_run.stdout and 'Action completed successfully.' in update_run.stdout)
        process.kill();process.wait();process=start()
        check('S14 process kill invalidates in-memory session',request('GET','/api/student/courses',token=student)[0]==401)
        student=login('student')
        check('S15 committed results survive process kill',ok('GET','/api/student/results',token=student)[0]['final_mark']==38)
        process.terminate();process.wait()
        snapshot=root/'backup'
        backup=subprocess.run(common+['--backup',str(snapshot)],capture_output=True,text=True)
        check('S16 backup command includes files',backup.returncode==0 and len(list((snapshot/'files').iterdir()))==1)
        restored=root/'restored.db'
        restore=subprocess.run([str(exe),'--db',str(restored),'--files',str(root/'restored-files'),'--restore',str(snapshot)],capture_output=True,text=True)
        check('S17 restore command',restore.returncode==0)
        with sqlite3.connect(restored) as db:
            check('S18 restored integrity and audit',db.execute('PRAGMA integrity_check').fetchone()[0]=='ok' and db.execute('SELECT COUNT(*) FROM mark_audit').fetchone()[0]==2)
        # Deliberately interrupt an uncommitted write, then check rollback on restart.
        child=subprocess.Popen([os.sys.executable,'-c','import sqlite3,time,sys; c=sqlite3.connect(sys.argv[1]); c.execute("BEGIN IMMEDIATE"); c.execute("UPDATE submission SET raw_mark=1"); print("ready",flush=True); time.sleep(60)',str(restored)],stdout=subprocess.PIPE,text=True)
        check('S19 fault writer opened transaction',child.stdout.readline().strip()=='ready')
        child.kill();child.wait()
        with sqlite3.connect(restored) as db:
            check('S20 interrupted transaction rolled back',db.execute('PRAGMA integrity_check').fetchone()[0]=='ok' and db.execute('SELECT raw_mark FROM submission').fetchone()[0]==38)
    finally:
        if process.poll() is None:
            process.terminate();process.wait()
output=Path(args.output);output.parent.mkdir(parents=True,exist_ok=True)
output.write_text(json.dumps({'suite':'HTTP system and automated acceptance scenarios','passed':sum(r['passed'] for r in records),'total':len(records),'results':records},indent=2))
print(f'{len(records)} checks passed; results: {output}')
