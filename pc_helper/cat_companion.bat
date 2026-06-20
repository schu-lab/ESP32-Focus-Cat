@echo off
rem Launches the Desk Cat helper hidden (no console window) using pythonw.
rem Put a shortcut to this file in your Startup folder (Win+R -> shell:startup)
rem to have the cat come alive automatically when you log in.
start "" pythonw "%~dp0cat_companion.py"
