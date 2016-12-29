git checkout dirtyOldDigitizer
make release
./efftest orchid_cfg < runcmds.txt
/home/prospect/DigitizerTester/digitizerClearer
git checkout newDigitizerReading
make release
./efftest orchid_cfg < runcmds.txt
