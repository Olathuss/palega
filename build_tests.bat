@echo off
echo Building INT 10h test...
wcl -fe=build\int10test.exe tests\int10test.c
wcl -fe=build\dumplog.exe tests\dumplog.c
echo All tests built.
