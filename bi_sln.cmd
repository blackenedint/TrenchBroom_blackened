@ECHO OFF
SET "TRENCHDIR=%~dp0"
SET "BUILDROOT=%TRENCHDIR%build

RMDIR /S /Q %BUILDROOT%

:: Later will output to another location; for now, since only vigil7 needs it immediately
:: just outputting there.
SET "INSTALLDIR=m:/bi/vigilseven/_tools/TrenchBroom"
:: trenchbroom cannot use our vcpkg Qt; it needs standalone version.
:: i **may** explicitly move it to %ROOTDIR%/thirdparty/Qt/6.7.3
:: but it's 15GB for just the required bits.
SET "QTDIR=m:\bi\Qt\6.9.2\msvc2022_64"

@rem SET VCVERSION="v143,version=14.38"
SET VCVERSION=v143

MKDIR "%BUILDROOT%"
PUSHD %BUILDROOT%
cmake %TRENCHDIR% -G"Visual Studio 17 2022" -T %VCVERSION% -A x64 ^
	-DCMAKE_PREFIX_PATH=%QTDIR% ^
	-DCMAKE_TOOLCHAIN_FILE=%TRENCHDIR%\vcpkg\scripts\buildsystems\vcpkg.cmake ^
	-DVCPKG_OVERLAY_PORTS=%TRENCHDIR%\vcpkg-overlay-ports ^
	-DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" ^
	-DCMAKE_INSTALL_PREFIX=%INSTALLDIR% ^
	-DTB_BLACKENED=ON ^
	-DTB_TESTS=OFF
@rem cmake %TRENCHDIR% -G "Visual Studio 17 2022" -A x64 -T ClangCL ^
@rem 	-DCMAKE_PREFIX_PATH="m:\bi\Qt\6.7.3\msvc2022_64" ^
@rem 	-D BUILD_TESTING=OFF
POPD
