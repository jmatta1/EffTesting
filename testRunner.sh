git checkout dirtyOldDigitizer
make release
./efftest orchid_cfg < runcmds.txt
make superclean
git checkout newDigitizerReading
make release
./efftest orchid_cfg < runcmds.txt
