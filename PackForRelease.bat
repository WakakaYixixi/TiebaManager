@echo off

ren ������ ���ɹ�����
copy /y Release\TiebaManager.exe ���ɹ�����
copy /y Release\Helper.dll ���ɹ�����
copy /y Release\TiebaAPI.dll ���ɹ�����
copy /y Release\TiebaManagerCore.dll ���ɹ�����
copy /y Release\Update.dll ���ɹ�����
copy /y Release\plugin\Defriend.dll ���ɹ�����\plugin
copy /y Release\plugin\LockThread.dll ���ɹ�����\plugin
copy /y Release\plugin\LoopBan.dll ���ɹ�����\plugin
del ���ɹ�����.zip >nul 2>nul
"F:\Program Files\7-Zip\7z.exe" a -tzip ���ɹ����� ���ɹ�����
ping 127.0.0.1 -n 3 >nul

set VER=%date:~2,8%
set VER=%VER:/=-%
set BACKUPDIR=��ʷ����\%VER%
md %BACKUPDIR% 2>nul
md %BACKUPDIR%\plugin 2>nul

move /y ���ɹ�����\TiebaManager.exe %BACKUPDIR%
move /y ���ɹ�����\Helper.dll %BACKUPDIR%
move /y ���ɹ�����\TiebaAPI.dll %BACKUPDIR%
move /y ���ɹ�����\TiebaManagerCore.dll %BACKUPDIR%
move /y ���ɹ�����\Update.dll %BACKUPDIR%
move /y ���ɹ�����\plugin\*.dll %BACKUPDIR%\plugin
copy /y Release\*.pdb %BACKUPDIR%
copy /y Release\plugin\*.pdb %BACKUPDIR%\plugin
ping 127.0.0.1 -n 3 >nul

ren ���ɹ����� ������

pause
