git checkout oldDigiNormOrd
make release
./efftest orchid_cfg < runcmds.txt
/home/prospect/DigitizerTester/digitizerClearer
git checkout oldDigiRevOrd
make release
./efftest orchid_cfg < runcmds.txt
