@echo off
setlocal DisableDelayedExpansion
"%~dp0tools\bringup\run_atlas.cmd" build %*
