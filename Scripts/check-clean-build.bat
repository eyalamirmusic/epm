@echo off
rem Windows twin of check-clean-build.sh: clones a branch of epm into a fresh
rem directory with an empty cache, builds it with clang-cl and runs every test.
rem
rem   Scripts\check-clean-build.bat VCVARS_FILE WORK_DIRECTORY [BRANCH], e.g. vcvars64.bat
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\%1" >nul
set W=%2
set B=%3
if "%B%"=="" set B=lockstep
if exist %W% rmdir /s /q %W%
mkdir %W%
git clone -q -b %B% https://github.com/jamierpond/epm %W%\src
cd /d %W%\src
git log --oneline -1
set EPM_CACHE=%W%\cache
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl > %W%\configure.log 2>&1
echo configure exit %ERRORLEVEL%
cmake --build build > %W%\build.log 2>&1
echo build exit %ERRORLEVEL%
ctest --test-dir build -j4 --output-on-failure > %W%\test.log 2>&1
findstr /C:"tests passed" /C:"***Failed" /C:"Not Run" %W%\test.log
