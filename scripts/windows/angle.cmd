@echo off
setlocal
if defined ANGLE_PYTHON (
  "%ANGLE_PYTHON%" "%~dp0angle.py" %*
) else (
  where py >nul 2>nul
  if not errorlevel 1 (
    py -3 "%~dp0angle.py" %*
  ) else (
    python "%~dp0angle.py" %*
  )
)
exit /b %errorlevel%
