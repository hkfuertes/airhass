setlocal

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"

set target=targets\win32\x86
set root=jansson
set build=%root%\build\
set pwd=%~dp0

if /I [%1] == [rebuild] (
	rd /q /s %build%
	set option=":Rebuild"
)

if not exist %build% (
	mkdir %build% && cd %build%
	cmake %pwd%\%root% -A Win32 -DJANSSON_BUILD_DOCS=OFF
	cd %pwd%
)	

msbuild libjansson.sln /property:Configuration=Debug -t:_last%option%
msbuild libjansson.sln /property:Configuration=Release -t:_last%option%

robocopy %build%\lib\Release %target% *.lib /NDL /NJH /NJS /nc /ns /np
robocopy %build%\lib\Debug %target% *.lib /NDL /NJH /NJS /nc /ns /np

robocopy jansson\src %target%\include jansson.h /NDL /NJH /NJS /nc /ns /np
robocopy %build%\include %target%\include jansson_config.h /NDL /NJH /NJS /nc /ns /np

del %target%\libjansson.lib && ren %target%\jansson.lib libjansson.lib
del %target%\libjansson_d.lib &&ren %target%\jansson_d.lib libjansson_d.lib

endlocal

