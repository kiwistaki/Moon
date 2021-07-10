@echo off
CALL  ..\libs\dxc\bin\x64\dxc.exe -E VS -T vs_6_0 -Fo mesh.vert.sco -Zpr -O3 mesh.vert.hlsl
CALL  ..\libs\dxc\bin\x64\dxc.exe -E PS -T ps_6_0 -Fo mesh.frag.cso -Zpr -O3 mesh.frag.hlsl
IF %ERRORLEVEL% NEQ 0 (
  PAUSE
)