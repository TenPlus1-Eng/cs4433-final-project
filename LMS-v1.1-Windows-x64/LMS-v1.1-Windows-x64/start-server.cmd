@echo off
cd /d "%~dp0"
if not exist lms.db lms_server.exe --init-admin admin
lms_server.exe --host 127.0.0.1 --port 8080
pause
