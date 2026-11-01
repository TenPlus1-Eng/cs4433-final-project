"""Synthetic loopback workload; target-hardware certification is separate."""
import concurrent.futures as futures
import ctypes
import hashlib
import json
import os
from pathlib import Path
import socket
import shutil
import sqlite3
import subprocess
import tempfile
import time
import urllib.request
import argparse

p=argparse.ArgumentParser();p.add_argument('--build',default='build');p.add_argument('--output',default='evidence/load.json');a=p.parse_args()
build=Path(a.build).resolve();exe=build/('lms_server.exe' if os.name=='nt' else 'lms_server')
work=build/'load-runtime';work.mkdir(exist_ok=True)
def percentile(values):return sorted(values)[int(.95*(len(values)-1))]
with tempfile.TemporaryDirectory(dir=work,ignore_cleanup_errors=True) as tmp:
    root=Path(tmp);dbpath=root/'load.db';db=sqlite3.connect(dbpath);db.executescript((build/'schema.sql').read_text())
    # Hash every synthetic account separately; each hash gets its own library salt.
    sodium=ctypes.CDLL(str(next(Path(d)/'libsodium-26.dll' for d in os.environ['PATH'].split(os.pathsep) if (Path(d)/'libsodium-26.dll').exists())) if os.name=='nt' else 'libsodium.so')
    sodium.sodium_init();sodium.crypto_pwhash_str_alg.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_ulonglong,ctypes.c_ulonglong,ctypes.c_size_t,ctypes.c_int]
    password=b'LoadTest123!';start=time.perf_counter()
    def account(i):
        out=ctypes.create_string_buffer(128)
        if sodium.crypto_pwhash_str_alg(out,password,len(password),2,67108864,2):raise RuntimeError('Hash failure')
        return (f'user{i}',f'Synthetic {i}','STUDENT' if i<800 else 'LECTURER',out.value.decode(),'2026-01-01T00:00:00Z')
    with futures.ThreadPoolExecutor(max_workers=2) as pool:
        db.executemany('INSERT INTO app_user(username,full_name,role,password_hash,created_at) VALUES(?,?,?,?,?)',pool.map(account,range(850)))
    print('850 uniquely salted synthetic accounts prepared',flush=True)
    db.execute("INSERT INTO department(name) VALUES('Synthetic')");db.execute("INSERT INTO course(code,title,department_id) VALUES('LOAD','Synthetic',1)")
    db.executemany("INSERT INTO enrolment(course_id,student_id,enrolled_at) VALUES(1,?,'2026-01-01T00:00:00Z')",[(i,) for i in range(1,801)])
    db.execute("INSERT INTO assignment(course_id,title,description,deadline,max_mark,created_by,created_at) VALUES(1,'Load','Synthetic','2099-01-01T00:00:00Z',50,801,'2026-01-01T00:00:00Z')");db.commit();db.close()
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    logfile=open(root/'server.log','w');process=subprocess.Popen([str(exe),'--db',str(dbpath),'--files',str(root/'files'),'--port',str(port)],stdout=logfile,stderr=subprocess.STDOUT)
    def req(method,path,data=None,token=None,ctype='application/json'):
        headers={'Content-Type':ctype}
        if token:headers['Authorization']='Bearer '+token
        r=urllib.request.Request(f'http://127.0.0.1:{port}'+path,data=data,headers=headers,method=method)
        with urllib.request.urlopen(r,timeout=180) as response:return json.loads(response.read())
    try:
        for _ in range(200):
            try:
                with socket.create_connection(('127.0.0.1',port),.1):break
            except OSError:time.sleep(.05)
        def login(i):
            begin=time.perf_counter();result=req('POST','/api/login',json.dumps({'username':f'user{i}','password':password.decode()}).encode());return result['token'],time.perf_counter()-begin
        with futures.ThreadPoolExecutor(max_workers=50) as pool:logins=list(pool.map(login,range(50)))
        login_times=[r[1] for r in logins]
        def listing(i):
            begin=time.perf_counter();req('GET','/api/student/courses',token=logins[i][0]);return time.perf_counter()-begin
        with futures.ThreadPoolExecutor(max_workers=50) as pool:list_times=list(pool.map(listing,range(50)))
        begin=time.perf_counter()
        def submit(i):
            token=logins[i][0] if i<50 else login(i)[0];payload=f'Synthetic coursework {i}'.encode();boundary='LOAD_BOUNDARY'
            raw=f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="work.txt"\r\nContent-Type: application/octet-stream\r\n\r\n'.encode()+payload+f'\r\n--{boundary}--\r\n'.encode()
            receipt=req('POST','/api/assignments/1/submission',raw,token,f'multipart/form-data; boundary={boundary}')
            if receipt['sha256']!=hashlib.sha256(payload).hexdigest():raise AssertionError('Receipt checksum')
            return True
        with futures.ThreadPoolExecutor(max_workers=50) as pool:accepted=sum(pool.map(submit,range(800)))
        elapsed=time.perf_counter()-begin
        print(f'800 submission requests completed in {elapsed:.2f}s',flush=True)
    finally:process.terminate();process.wait();logfile.close()
    with sqlite3.connect(dbpath) as db:
        files=db.execute('SELECT stored_path,sha256 FROM submission_file').fetchall()
        verified=sum(hashlib.sha256((root/'files'/path).read_bytes()).hexdigest()==digest for path,digest in files)
        integrity=db.execute('PRAGMA integrity_check').fetchone()[0]
    report={'environment':'Windows x64 loopback; not constrained to specified 2 CPU cores / 4 GB','accounts':850,'students':800,'staff':50,'concurrency':50,'login_p95_seconds':round(percentile(login_times),4),'listing_p95_seconds':round(percentile(list_times),4),'submission_seconds':round(elapsed,3),'accepted':accepted,'checksums_verified':verified,'integrity_check':integrity,'NFR01_local_pass':percentile(login_times)<=2,'NFR02_local_pass':percentile(list_times)<=2,'NFR03_local_pass':elapsed<600 and accepted==800 and verified==800,'target_hardware_certified':False,'external_network_physically_disabled':False}
    Path(a.output).write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
