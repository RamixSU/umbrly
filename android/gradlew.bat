@echo off
setlocal

set "JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot"
set "GRADLE_HOME=C:\Users\Ramix\.gradle\wrapper\dists\gradle-9.5.1-bin\iq79hdu3mqx29lgffhp8bfmx\gradle-9.5.1"
set "PATH=%JAVA_HOME%\bin;C:\msys64\mingw64\bin;%GRADLE_HOME%\bin;%PATH%"

"%GRADLE_HOME%\bin\gradle.bat" %*
